#pragma once
#include <nxui/core/Types.hpp>
#include "Texture.hpp"
#include <SDL2/SDL_ttf.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <list>

namespace nxui {

class Renderer;
class GpuDevice;

class Font {
public:
    Font() = default;
    ~Font();

    bool load(GpuDevice& gpu, Renderer& ren,
              const std::string& path, int ptSize);

    // Same, from a font file already in memory. SDL_ttf reads glyphs from the
    // source for as long as the font lives, so the bytes are not copied and
    // must outlive it -- in exchange, no file stays open.
    bool loadFromMemory(GpuDevice& gpu, Renderer& ren,
                        const void* data, std::size_t size, int ptSize);

    // Render a string to a texture (cached) and draw it
    void draw(Renderer& ren, const std::string& text,
              const Vec2& pos, const Color& color, float scale = 1.f);

    // Measure text dimensions
    Vec2 measure(const std::string& text) const;

    // Clear all cached glyph textures (call before GPU descriptor reset)
    void clearCache();

    bool maintenanceRequested() const { return m_maintenanceRequested; }
    std::size_t cacheBytes() const { return m_cacheBytes; }
    std::size_t cacheEntryCount() const { return m_lruList.size(); }
    void trimCache(std::size_t maxEntries = 72,
                   std::size_t maxBytes = 2u * 1024u * 1024u);

    int ptSize() const { return m_ptSize; }
    std::uint64_t revision() const { return m_revision; }
    bool valid() const { return m_font != nullptr; }

private:
    // Render full string to texture (cache by string)
    Texture* getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text);
    void adopt(GpuDevice& gpu, Renderer& ren, TTF_Font* newFont, int ptSize);

    TTF_Font* m_font = nullptr;
    int       m_ptSize = 0;
    GpuDevice* m_gpu = nullptr;
    Renderer*  m_ren = nullptr;
    std::uint64_t m_revision = 0;

    // LRU string-texture cache
    // The list stores entries in LRU order (most-recently-used at front).
    // The map provides O(1) lookup by string → list iterator.
    // On eviction, the Texture of the least-recently-used entry is reused
    // (its descriptor slot and MemBlock are recycled via loadFromPixels).
    static constexpr size_t kMaxCacheEntries = 384;

    struct CacheEntry {
        std::string key;
        Texture     tex;
        int         w = 0, h = 0;
    };
    using LruList = std::list<CacheEntry>;
    using LruMap  = std::unordered_map<std::string, LruList::iterator>;

    LruList m_lruList;
    LruMap  m_lruMap;
    std::size_t m_cacheBytes = 0;
    bool m_maintenanceRequested = false;
};

} // namespace nxui
