/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include "geostudio/native/globe/ElevationSampler.h"
#include "geostudio/native/globe/Cache.h"

using namespace geostudio::native;

ElevationSampler::ElevationSampler()
{
    cache = std::make_shared<detail::LRUCache<TileKey, Result<GeoImage>>>(64);
}

auto ElevationSampler::fetch(const TileKey& key, const IOOptions& io) const -> Result<GeoImage>
{
    // check the cache first.
    if (cache)
    {
        auto r = cache->get(key);
        if (r.has_value())
        {
            return r.value();
        }
    }

    // failing that, check the layer, and fall back to parent tiles if necessary.
    for (auto k = key; k.valid(); k.makeParent())
    {
        auto r = layer->createTile(k, io);
        if (r.ok())
        {
            if (cache)
                cache->put(key, r);
            return r;
        }
    }

    return Failure{};
}
