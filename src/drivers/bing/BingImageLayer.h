/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once
#include <geostudio/native/drivers/bing/Bing.h>
#ifdef GEOSTUDIO_NATIVE_HAS_BING

#include <geostudio/native/scene/layer/ImageLayer.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    /**
     * Image layer reading from Microsoft's Bing Maps API
     */
    class GEOSTUDIO_NATIVE_API BingImageLayer : public Inherit<ImageLayer, BingImageLayer>, public Bing::ImageLayerOptions
    {
    public:
        //! Construct an empty Bing layer
        BingImageLayer();

        //! Deserialize a Bing maps image layer
        BingImageLayer(std::string_view JSON, const IOOptions& io);

        //! serialize
        std::string to_json() const override;

    protected: // Layer

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;

        //! Creates a raster image for the given tile key
        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override;

    private:

        void construct(std::string_view, const IOOptions& io);
    };
}


#else // if !GEOSTUDIO_NATIVE_HAS_BING
#ifndef GEOSTUDIO_NATIVE_BUILDING_SDK
#error Bing support is not enabled in Rocky.
#endif
#endif // GEOSTUDIO_NATIVE_HAS_BING
