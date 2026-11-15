#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace nxui {

namespace {

// Next UTF-8 code point; malformed bytes come back as U+FFFD, one at a time.
std::uint32_t nextCodepoint(const std::string& s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int extra = 0;
    std::uint32_t cp = 0;
    if (c < 0x80) { ++i; return c; }
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { ++i; return 0xFFFD; }
    for (int k = 1; k <= extra; ++k) {
        if (i + k >= s.size()) { ++i; return 0xFFFD; }
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

// Marks with nothing to draw on their own: zero-width joiners and spaces,
// variation selectors (the emoji/text style switch after a symbol), skin-tone
// modifiers and tag characters. A face without them would draw a box.
bool invisibleMark(std::uint32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
           (cp >= 0xE0020 && cp <= 0xE007F) || cp == 0xFEFF;
}

// Styled letters that are plain ASCII underneath, for when no face has them:
// the Mathematical Alphanumeric Symbols block (bold, sans, script... letters
// and digits, popular in video titles) and fullwidth ASCII. Returns 0 when
// the code point has no ASCII form.
char asciiFold(std::uint32_t cp) {
    if (cp >= 0x1D400 && cp <= 0x1D6A3) {
        const unsigned idx = (cp - 0x1D400) % 52;
        return static_cast<char>(idx < 26 ? 'A' + idx : 'a' + (idx - 26));
    }
    if (cp == 0x1D6A4) return 'i';
    if (cp == 0x1D6A5) return 'j';
    if (cp >= 0x1D7CE && cp <= 0x1D7FF)
        return static_cast<char>('0' + (cp - 0x1D7CE) % 10);
    if (cp >= 0xFF01 && cp <= 0xFF5E)
        return static_cast<char>(cp - 0xFF01 + 0x21);
    return 0;
}

} // namespace

std::vector<Font::FallbackFace>& Font::fallbackFaces() {
    static std::vector<FallbackFace> faces;
    return faces;
}

void Font::addFallbackFace(const void* data, std::size_t size, const char* name) {
    if (!data || size == 0)
        return;
    fallbackFaces().push_back({data, size, name ? name : "", false});
}

bool Font::addFallbackFile(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe)
        return false;
    // Read on first use, not here: most sessions never draw an emoji.
    fallbackFaces().push_back({nullptr, 0, path, false});
    return true;
}

bool Font::ensureFallbackLoaded(std::size_t index) {
    auto& faces = fallbackFaces();
    if (index >= faces.size())
        return false;
    FallbackFace& f = faces[index];
    if (f.data)
        return true;
    if (f.failed)
        return false;
    f.failed = true;
    std::ifstream in(f.name, std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return false;
    // Kept for the life of the process: faces opened from it read it lazily.
    auto* bytes = new std::vector<std::uint8_t>(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes->data()), size);
    if (!in) {
        delete bytes;
        return false;
    }
    f.data = bytes->data();
    f.size = bytes->size();
    f.failed = false;
    return true;
}

void Font::closeFallbacks() {
    for (TTF_Font* f : m_fallbackFonts)
        if (f) TTF_CloseFont(f);
    m_fallbackFonts.clear();
    m_fallbackTried.clear();
    m_faceCache.clear();
}

TTF_Font* Font::face(int index) const {
    if (index == 0)
        return m_font;
    const std::size_t fb = static_cast<std::size_t>(index - 1);
    const auto& faces = fallbackFaces();
    if (fb >= faces.size() || !m_font)
        return nullptr;
    if (m_fallbackFonts.size() < faces.size()) {
        m_fallbackFonts.resize(faces.size(), nullptr);
        m_fallbackTried.resize(faces.size(), false);
    }
    if (!m_fallbackTried[fb]) {
        m_fallbackTried[fb] = true;
        if (!ensureFallbackLoaded(fb))
            return nullptr;
        const int rasterPt = static_cast<int>(m_ptSize * m_rasterScale + 0.5f);
        SDL_RWops* rw = SDL_RWFromConstMem(faces[fb].data, static_cast<int>(faces[fb].size));
        TTF_Font* f = rw ? TTF_OpenFontRW(rw, 1, rasterPt) : nullptr;
        if (f)
            TTF_SetFontHinting(f, TTF_HINTING_NONE);
        else
            std::fprintf(stderr, "[Font] fallback %s failed: %s\n", faces[fb].name.c_str(), TTF_GetError());
        m_fallbackFonts[fb] = f;
    }
    return m_fallbackFonts[fb];
}

int Font::faceFor(std::uint32_t cp) const {
    if (!m_font)
        return -1;
    if (cp < 0x80)
        return 0;
    const auto known = m_faceCache.find(cp);
    if (known != m_faceCache.end())
        return known->second;
    int found = -1;
    if (TTF_GlyphIsProvided32(m_font, static_cast<Uint32>(cp)))
        found = 0;
    for (std::size_t i = 0; found < 0 && i < fallbackFaces().size(); ++i) {
        TTF_Font* f = face(static_cast<int>(i + 1));
        if (f && TTF_GlyphIsProvided32(f, static_cast<Uint32>(cp)))
            found = static_cast<int>(i + 1);
    }
    if (m_faceCache.size() > 8192)
        m_faceCache.clear();
    m_faceCache.emplace(cp, static_cast<std::int8_t>(found));
    return found;
}

bool Font::splitRuns(const std::string& text, std::vector<Run>& runs) const {
    // Fast path: plain ASCII, or nothing registered to fall back on.
    bool ascii = true;
    for (unsigned char c : text)
        if (c >= 0x80) { ascii = false; break; }
    if (ascii || fallbackFaces().empty())
        return false;

    bool needed = false;
    runs.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = nextCodepoint(text, i);
        if (invisibleMark(cp)) {
            needed = true; // dropped
            continue;
        }
        int f = faceFor(cp);
        if (f < 0) {
            if (const char folded = asciiFold(cp)) {
                // A styled letter no face draws: show the plain letter.
                needed = true;
                if (runs.empty() || runs.back().face != 0)
                    runs.push_back({0, {}});
                runs.back().text.push_back(folded);
                continue;
            }
            // No face has it: leave it to the primary (its box), but in its
            // own run so its neighbours still use their faces.
            f = 0;
        }
        if (f != 0)
            needed = true;
        if (runs.empty() || runs.back().face != f)
            runs.push_back({f, {}});
        runs.back().text.append(text, start, i - start);
    }
    return needed;
}

SDL_Surface* Font::renderRuns(const std::vector<Run>& runs) const {
    SDL_Color white = {255, 255, 255, 255};
    struct Piece { SDL_Surface* s; int ascent; };
    std::vector<Piece> pieces;
    int width = 0, maxAscent = 0, maxBelow = 0;
    for (const auto& run : runs) {
        TTF_Font* f = face(run.face);
        if (!f || run.text.empty())
            continue;
        SDL_Surface* s = TTF_RenderUTF8_Blended(f, run.text.c_str(), white);
        if (!s)
            continue;
        const int ascent = TTF_FontAscent(f);
        pieces.push_back({s, ascent});
        width += s->w;
        maxAscent = std::max(maxAscent, ascent);
        maxBelow = std::max(maxBelow, s->h - ascent);
    }
    SDL_Surface* out = nullptr;
    if (width > 0 && maxAscent + maxBelow > 0)
        out = SDL_CreateRGBSurfaceWithFormat(0, width, maxAscent + maxBelow, 32, SDL_PIXELFORMAT_ARGB8888);
    if (out) {
        SDL_FillRect(out, nullptr, 0);
        int x = 0;
        for (const auto& p : pieces) {
            // Runs do not overlap: copy, do not blend onto the transparent
            // canvas (blending would darken the antialiased edges).
            SDL_SetSurfaceBlendMode(p.s, SDL_BLENDMODE_NONE);
            SDL_Rect dst{x, maxAscent - p.ascent, p.s->w, p.s->h};
            SDL_BlitSurface(p.s, nullptr, out, &dst);
            x += p.s->w;
        }
    }
    for (auto& p : pieces)
        SDL_FreeSurface(p.s);
    return out;
}

Font::~Font() {
    closeFallbacks();
    if (m_font) TTF_CloseFont(m_font);
}

bool Font::load(GpuDevice& gpu, Renderer& ren,
                const std::string& path, int ptSize)
{
    // Read the face into RAM once. Opened from the SD card, every new glyph
    // read its tables from the card (a few ms each): the stutter when a
    // window or tab showed text for the first time. Approach from HomeNX
    // lib/nxui/src/core/Font.cpp @ 0448cd4 (GPL-3.0, ad3333m).
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "[Font] open failed: %s\n", path.c_str());
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in || data.empty()) {
        std::fprintf(stderr, "[Font] read failed: %s\n", path.c_str());
        return false;
    }
    SDL_RWops* rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
    const int rasterPt = static_cast<int>(ptSize * GpuDevice::renderScale() + 0.5f);
    TTF_Font* newFont = rw ? TTF_OpenFontRW(rw, 1, rasterPt) : nullptr;
    if (!newFont) {
        // SDL_ttf closes the stream itself on failure when freesrc is set
        // (SDL_ttf 2.0.15+), so it is not closed again here.
        std::fprintf(stderr, "[Font] TTF_OpenFontRW failed: %s\n", TTF_GetError());
        return false;
    }
    adopt(gpu, ren, newFont, ptSize);
    m_fontData = std::move(data); // after adopt closed the previous face
    return true;
}

bool Font::loadFromMemory(GpuDevice& gpu, Renderer& ren,
                          const void* data, std::size_t size, int ptSize)
{
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    const int rasterPt = static_cast<int>(ptSize * GpuDevice::renderScale() + 0.5f);
    TTF_Font* newFont = rw ? TTF_OpenFontRW(rw, 1, rasterPt) : nullptr;
    if (!newFont) {
        std::fprintf(stderr, "[Font] TTF_OpenFontRW failed: %s\n", TTF_GetError());
        return false;
    }
    adopt(gpu, ren, newFont, ptSize);
    return true;
}

void Font::adopt(GpuDevice& gpu, Renderer& ren, TTF_Font* newFont, int ptSize) {
    closeFallbacks(); // reopened at the new size when needed
    if (m_font)
        TTF_CloseFont(m_font);
    m_fontData.clear(); // the closed face no longer reads it
    // Unhinted glyphs rasterize about ten times faster in this FreeType build
    // and look the same at UI sizes (HomeNX perf.log, build 176).
    TTF_SetFontHinting(newFont, TTF_HINTING_NONE);
    m_measureCache.clear();

    m_font = newFont;
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;
    // Text is laid out in logical pixels; a 1.5x frame gets 1.5x glyphs.
    m_rasterScale = GpuDevice::renderScale();
    ++m_revision;
    clearCache();
}

Vec2 Font::measure(const std::string& text) const {
    if (!m_font || text.empty()) return {0, 0};
    // Rendered strings already carry their exact dimensions. Avoid invoking
    // SDL_ttf again for every label on every frame.
    const auto cached = m_lruMap.find(text);
    if (cached != m_lruMap.end())
        return {static_cast<float>(cached->second->w) / m_rasterScale,
                static_cast<float>(cached->second->h) / m_rasterScale};
    const auto known = m_measureCache.find(text);
    if (known != m_measureCache.end())
        return known->second;
    int w = 0, h = 0;
    std::vector<Run> runs;
    if (splitRuns(text, runs)) {
        int maxAscent = 0, maxBelow = 0;
        for (const auto& run : runs) {
            TTF_Font* f = face(run.face);
            int rw = 0, rh = 0;
            if (!f || run.text.empty() || TTF_SizeUTF8(f, run.text.c_str(), &rw, &rh) != 0)
                continue;
            const int ascent = TTF_FontAscent(f);
            w += rw;
            maxAscent = std::max(maxAscent, ascent);
            maxBelow = std::max(maxBelow, rh - ascent);
        }
        h = maxAscent + maxBelow;
    } else {
        TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
    }
    const Vec2 size{(float)w / m_rasterScale, (float)h / m_rasterScale};
    // Wrapped-text layout measures every growing prefix of a line, every
    // frame; keep the answers. Bounded: cleared when it grows past the cap.
    if (m_measureCache.size() >= kMeasureCacheMax)
        m_measureCache.clear();
    m_measureCache.emplace(text, size);
    return size;
}

void Font::clearCache() {
    m_lruList.clear();
    m_lruMap.clear();
    m_cacheBytes = 0;
    m_maintenanceRequested = false;
}

void Font::trimCache(std::size_t maxEntries, std::size_t maxBytes) {
    while (!m_lruList.empty() &&
           (m_lruList.size() > maxEntries || m_cacheBytes > maxBytes)) {
        auto victim = std::prev(m_lruList.end());
        const std::size_t bytes = victim->tex.allocationSize();
        m_lruMap.erase(victim->key);
        m_lruList.erase(victim);
        m_cacheBytes = bytes <= m_cacheBytes ? m_cacheBytes - bytes : 0;
    }
    m_maintenanceRequested = false;
}

Texture* Font::getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text) {
    // Cache hit: promote to the front.
    auto it = m_lruMap.find(text);
    if (it != m_lruMap.end()) {
        // Move to front (most-recently-used)
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        return &it->second->tex;
    }

    // Full: drop the least-recently-used string. Its texture is retired to
    // the renderer and freed frames later, so this does not wait for the GPU
    // the way reloading that texture in place did (every new string past the
    // cap used to stall a frame). Owners that trim on the flag still can.
    if (m_lruList.size() >= kMaxCacheEntries) {
        m_maintenanceRequested = true;
        auto victim = std::prev(m_lruList.end());
        const std::size_t bytes = victim->tex.allocationSize();
        m_lruMap.erase(victim->key);
        m_lruList.erase(victim);
        m_cacheBytes = bytes <= m_cacheBytes ? m_cacheBytes - bytes : 0;
    }
    // A raster costs milliseconds on the render thread, and a screen change
    // meets dozens of new strings in one frame: spread them, a few per frame,
    // shared by every font (HomeNX lib/nxui Font.cpp @ 0448cd4, GPL-3.0).
    {
        const std::uint64_t now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now - s_rasterWindow > 16000) {
            s_rasterWindow = now;
            s_rastersInWindow = 0;
        }
        if (s_rastersInWindow >= kMaxRastersPerFrame)
            return nullptr; // drawn on a following frame
        ++s_rastersInWindow;
    }

    // Render the string via SDL_ttf.
    SDL_Color white = {255, 255, 255, 255};
    std::vector<Run> runs;
    SDL_Surface* surface = splitRuns(text, runs)
        ? renderRuns(runs)
        : TTF_RenderUTF8_Blended(m_font, text.c_str(), white);
    if (!surface) return nullptr;

    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surface);
    if (!rgba) return nullptr;

    SDL_LockSurface(rgba);
    const uint8_t* pixels = static_cast<const uint8_t*>(rgba->pixels);
    int w = rgba->w, h = rgba->h, pitch = rgba->pitch;

    m_lruList.emplace_front();
    auto& entry = m_lruList.front();
    entry.key = text;
    entry.w = w;
    entry.h = h;
    if (!entry.tex.loadFromSurface(gpu, ren, pixels, w, h, pitch)) {
        m_lruList.pop_front();
        m_maintenanceRequested = true;
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);
        return nullptr;
    }
    m_cacheBytes += entry.tex.allocationSize();
    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);

    m_lruMap[text] = m_lruList.begin();
    return &m_lruList.front().tex;
}

void Font::draw(Renderer& ren, const std::string& text,
                const Vec2& pos, const Color& color, float scale) {
    if (!m_font || !m_gpu || text.empty()) return;

    Texture* tex = getOrRender(*m_gpu, ren, text);
    if (!tex || !tex->valid()) return;

    float w = tex->width()  * scale / m_rasterScale;
    float h = tex->height() * scale / m_rasterScale;
    ren.drawTexture(tex, {pos.x, pos.y, w, h}, color);
}

} // namespace nxui
