/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once
#include <geostudio/native/drivers/azure/Azure.h>
#ifdef GEOSTUDIO_NATIVE_HAS_AZURE

#include <geostudio/native/scene/layer/ImageLayer.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    /**
     * Image layer reading from Microsoft's Azure Maps REST API
     * https://learn.microsoft.com/en-us/rest/api/maps/render/get-map-tile?view=rest-maps-2024-04-01
     */
    class GEOSTUDIO_NATIVE_API AzureImageLayer : public Inherit<ImageLayer, AzureImageLayer>,
        public Azure::Options
    {
    public:
        //! Construct an empty Azure layer
        AzureImageLayer();

        //! Deserialize an Azure Maps layer
        AzureImageLayer(std::string_view JSON, const IOOptions& io);

        //! serialize
        std::string to_json() const override;

    protected: // Layer

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;

        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override;

    private:

        void construct(std::string_view JSON, const IOOptions& io);

        URI::Context _uriContext;
    };
}


#else // if !GEOSTUDIO_NATIVE_HAS_AZURE
#ifndef GEOSTUDIO_NATIVE_BUILDING_SDK
#error Azure support is not enabled in Rocky.
#endif
#endif // GEOSTUDIO_NATIVE_HAS_AZURE
