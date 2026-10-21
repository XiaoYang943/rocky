/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/geo/tiling/TileKey.h>
#include <geostudio/native/data/raster/GeoImage.h>
#include <vector>

namespace geostudio::native
{
    class Image;
    class Heightfield;
    class Layer;
    class Map;

    /**
     * Data model backing an individual terrain tile.
     */
    struct TerrainTileModel
    {
    public:
        struct GEOSTUDIO_NATIVE_API Tile
        {
            TileKey key;
            Revision revision = -1;
            glm::fmat4 matrix = glm::fmat4(1.0f); // identity
        };

        struct GEOSTUDIO_NATIVE_API ColorLayer : public Tile
        {
            GeoImage image;
            std::shared_ptr<const Layer> layer;
            using Vector = std::vector<ColorLayer>;
        };

        struct GEOSTUDIO_NATIVE_API Elevation : public Tile
        {
            GeoImage heightfield;
        };

        struct GEOSTUDIO_NATIVE_API NormalMap : public Tile
        {
            GeoImage image;
            std::shared_ptr<const Layer> layer;
        };

        struct GEOSTUDIO_NATIVE_API MaterialMap : public Tile
        {
        };

        //! True if this model contains no data.
        bool empty() const
        {
            return colorLayers.empty() && !elevation.heightfield;
        }

        //! Map model revision from which this model was created
        Revision revision = -1;

        //! Tile key corresponding to this model
        TileKey key;


        //! Imagery and other surface coloring layers
        ColorLayer::Vector colorLayers;

        //! Elevation data
        Elevation elevation;

        //! Normal map data
        NormalMap normalMap;

    };

}
