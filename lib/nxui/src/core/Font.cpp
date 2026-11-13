#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iterator>

namespace nxui {

Font::~Font() {
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
    TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
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
    SDL_Surface* surface = TTF_RenderUTF8_Blended(m_font, text.c_str(), white);
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
