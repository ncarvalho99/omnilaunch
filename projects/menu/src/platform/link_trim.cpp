// Keeps code the menu never runs out of the linked binary.
//
// The menu process is created again on every HOME return, and the system
// loader reads and decompresses the whole executable from the SD card each
// time. Two static libraries pulled in far more than the menu uses:
//
// - SDL2 registers its Switch video driver in a table inside SDL_video.o, and
//   that driver links EGL and the whole Mesa/nouveau OpenGL stack. The menu
//   draws with deko3d and starts only SDL's audio subsystem.
// - FFmpeg's registries (codecs, formats, protocols, parsers, bitstream
//   filters) list every component the library was built with, so every
//   decoder and demuxer was linked. The wallpaper player accepts only H.264
//   (VideoPlayer.cpp, wallpaperStreamRejection).
//
// The linker takes a symbol from an archive member only when nothing defined
// it first. Defining every symbol of those registry members here keeps the
// members, and everything only they reference, out of the binary. The lists
// below are the components the menu actually uses; add an entry here before
// relying on any other codec, container or protocol.

#if defined(__SWITCH__) && !defined(NXUI_BACKEND_SDL2)

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avstring.h>
#include <libavutil/mem.h>
}

#define OMNI_KEEP __attribute__((used, visibility("default")))

// ---------------------------------------------------------------------------
// SDL2: an inert video driver entry. Same layout as SDL's VideoBootStrap
// (SDL 2.28 src/video/SDL_sysvideo.h). SDL_VideoInit is never called by the
// menu; if it were, a null device makes it fail cleanly ("No available video
// device") instead of starting a second renderer.
// ---------------------------------------------------------------------------
extern "C" {
struct OmniVideoBootStrap {
    const char* name;
    const char* desc;
    void* (*create)(void);
};

static void* omniNoVideoDevice(void) { return nullptr; }

OMNI_KEEP OmniVideoBootStrap SWITCH_bootstrap = {
    "switch", "Video driver not linked", &omniNoVideoDevice,
};
}

// ---------------------------------------------------------------------------
// FFmpeg 7.1 registries. Every FF* wrapper struct (FFCodec, FFInputFormat,
// FFOutputFormat, FFBitStreamFilter) begins with its public struct, so the
// address of the wrapper is the address of the public object, which is what
// the original registries return.
// ---------------------------------------------------------------------------
extern "C" {
extern const AVCodec ff_h264_decoder;
extern const AVCodec ff_aac_decoder;  // lets stream probing finish on the audio track

extern const AVInputFormat ff_mov_demuxer;
extern const AVInputFormat ff_matroska_demuxer;
extern const AVInputFormat ff_avi_demuxer;

extern const AVCodecParser ff_h264_parser;
extern const AVCodecParser ff_aac_parser;

extern const AVBitStreamFilter ff_null_bsf;
extern const AVBitStreamFilter ff_extract_extradata_bsf;
extern const AVBitStreamFilter ff_h264_mp4toannexb_bsf;

struct URLProtocol;  // private libavformat type; only its name and class are read
extern const URLProtocol ff_file_protocol;
}

namespace {

const AVCodec* const kCodecs[] = {&ff_h264_decoder, &ff_aac_decoder, nullptr};
const AVInputFormat* const kDemuxers[] = {&ff_mov_demuxer, &ff_matroska_demuxer, &ff_avi_demuxer, nullptr};
const AVCodecParser* const kParsers[] = {&ff_h264_parser, &ff_aac_parser, nullptr};
const AVBitStreamFilter* const kBsfs[] = {&ff_null_bsf, &ff_extract_extradata_bsf, &ff_h264_mp4toannexb_bsf, nullptr};
const URLProtocol* const kProtocols[] = {&ff_file_protocol, nullptr};

// First fields of libavformat's URLProtocol (libavformat/url.h, 7.1): name at
// offset 0, priv_data_class after the 13 callbacks. Read through this view
// only; the real struct is never copied or written.
struct UrlProtocolView {
    const char* name;
    void* callbacks[13];
    const AVClass* priv_data_class;
};
static_assert(offsetof(UrlProtocolView, priv_data_class) == 14 * sizeof(void*));

const UrlProtocolView* view(const URLProtocol* p) {
    return reinterpret_cast<const UrlProtocolView*>(p);
}

template <typename T>
const T* iterateList(const T* const* list, void** opaque) {
    const uintptr_t i = reinterpret_cast<uintptr_t>(*opaque);
    const T* item = list[i];
    if (item)
        *opaque = reinterpret_cast<void*>(i + 1);
    return item;
}

const AVCodec* findCodec(AVCodecID id, int (*accept)(const AVCodec*)) {
    const AVCodec* experimental = nullptr;
    for (const AVCodec* const* c = kCodecs; *c; ++c) {
        if (!accept(*c) || (*c)->id != id)
            continue;
        if (((*c)->capabilities & AV_CODEC_CAP_EXPERIMENTAL) && !experimental)
            experimental = *c;
        else
            return *c;
    }
    return experimental;
}

const AVCodec* findCodecByName(const char* name, int (*accept)(const AVCodec*)) {
    if (!name)
        return nullptr;
    for (const AVCodec* const* c = kCodecs; *c; ++c)
        if (accept(*c) && std::strcmp(name, (*c)->name) == 0)
            return *c;
    return nullptr;
}

}  // namespace

extern "C" {

// libavcodec/allcodecs.c
OMNI_KEEP const AVCodec* av_codec_iterate(void** opaque) { return iterateList(kCodecs, opaque); }
OMNI_KEEP const AVCodec* avcodec_find_decoder(enum AVCodecID id) { return findCodec(id, av_codec_is_decoder); }
OMNI_KEEP const AVCodec* avcodec_find_encoder(enum AVCodecID id) { return findCodec(id, av_codec_is_encoder); }
OMNI_KEEP const AVCodec* avcodec_find_decoder_by_name(const char* name) { return findCodecByName(name, av_codec_is_decoder); }
OMNI_KEEP const AVCodec* avcodec_find_encoder_by_name(const char* name) { return findCodecByName(name, av_codec_is_encoder); }

// libavformat/allformats.c (no muxers, no devices)
OMNI_KEEP const AVInputFormat* av_demuxer_iterate(void** opaque) { return iterateList(kDemuxers, opaque); }
OMNI_KEEP const AVOutputFormat* av_muxer_iterate(void**) { return nullptr; }
OMNI_KEEP void avpriv_register_devices(const void* const*, const void* const*) {}

// libavcodec/parsers.c
OMNI_KEEP const AVCodecParser* av_parser_iterate(void** opaque) { return iterateList(kParsers, opaque); }

// libavcodec/bitstream_filters.c
OMNI_KEEP const AVBitStreamFilter* av_bsf_iterate(void** opaque) { return iterateList(kBsfs, opaque); }
OMNI_KEEP const AVBitStreamFilter* av_bsf_get_by_name(const char* name) {
    if (!name)
        return nullptr;
    for (const AVBitStreamFilter* const* f = kBsfs; *f; ++f)
        if (std::strcmp((*f)->name, name) == 0)
            return *f;
    return nullptr;
}
OMNI_KEEP const AVClass* ff_bsf_child_class_iterate(void** opaque) {
    while (const AVBitStreamFilter* f = av_bsf_iterate(opaque))
        if (f->priv_class)
            return f->priv_class;
    return nullptr;
}

// libavformat/protocols.c
OMNI_KEEP const AVClass* ff_urlcontext_child_class_iterate(void** iter) {
    uintptr_t i = reinterpret_cast<uintptr_t>(*iter);
    const AVClass* ret = nullptr;
    for (; kProtocols[i]; ++i) {
        ret = view(kProtocols[i])->priv_data_class;
        if (ret)
            break;
    }
    *iter = reinterpret_cast<void*>(kProtocols[i] ? i + 1 : i);
    return ret;
}
OMNI_KEEP const char* avio_enum_protocols(void** opaque, int output) {
    // Only "file" is linked; it reads and writes.
    uintptr_t i = reinterpret_cast<uintptr_t>(*opaque);
    (void)output;
    if (kProtocols[i]) {
        *opaque = reinterpret_cast<void*>(i + 1);
        return view(kProtocols[i])->name;
    }
    *opaque = nullptr;
    return nullptr;
}
OMNI_KEEP const AVClass* avio_protocol_get_class(const char* name) {
    for (const URLProtocol* const* p = kProtocols; *p; ++p)
        if (std::strcmp(view(*p)->name, name) == 0)
            return view(*p)->priv_data_class;
    return nullptr;
}
OMNI_KEEP const URLProtocol** ffurl_get_protocols(const char* whitelist, const char* blacklist) {
    constexpr size_t kCount = sizeof(kProtocols) / sizeof(kProtocols[0]);
    auto** ret = static_cast<const URLProtocol**>(av_calloc(kCount, sizeof(const URLProtocol*)));
    if (!ret)
        return nullptr;
    size_t n = 0;
    for (const URLProtocol* const* p = kProtocols; *p; ++p) {
        const char* pname = view(*p)->name;
        if (whitelist && *whitelist && !av_match_name(pname, whitelist))
            continue;
        if (blacklist && *blacklist && av_match_name(pname, blacklist))
            continue;
        ret[n++] = *p;
    }
    return ret;
}

}  // extern "C"

#endif
