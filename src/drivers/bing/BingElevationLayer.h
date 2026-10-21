/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once
#include <geostudio/native/drivers/bing/Bing.h>

#ifdef GEOSTUDIO_NATIVE_HAS_BING

#include <geostudio/native/scene/layer/ElevationLayer.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    /**
     * Elevation layer reading from from Microsoft's Bing Maps API
     */
    class GEOSTUDIO_NATIVE_API BingElevationLayer : public Inherit<ElevationLayer, BingElevationLayer>,
        public Bing::ElevationLayerOptions
    {
    public:
        //! Construct an empty Bing layer
        BingElevationLayer();
        BingElevationLayer(std::string_view JSON, const IOOptions& io);

        //! Serialize
        std::string to_json() const override;

        option<Encoding> encoding;

    public: // Layer

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;

        //! Creates a raster image for the given tile key
        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override;


    private:
        void construct(std::string_view JSON, const IOOptions& io);
    };
}

#else // if !GEOSTUDIO_NATIVE_HAS_BING
#ifndef GEOSTUDIO_NATIVE_BUILDING_SDK
#error Bing support is not enabled in Rocky.
#endif
#endif // GEOSTUDIO_NATIVE_HAS_BING
