/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/drivers/tms/TMS.h>
#include <geostudio/native/scene/layer/ElevationLayer.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    /**
     * Elevation layer reading from a TMS (Tile Map Service) endpoint
     */
    class GEOSTUDIO_NATIVE_API TMSElevationLayer : public Inherit<ElevationLayer, TMSElevationLayer>,
        public TMS::Options
    {
    public:
        //! Construct an empty TMS layer
        TMSElevationLayer();
        TMSElevationLayer(std::string_view JSON, const IOOptions& io);

        //! Serialize
        std::string to_json() const override;

        //! extent
        const GeoExtent& extent() const override;

    public: // Layer

        Result<> openImplementation(const IOOptions& io) override;

        void closeImplementation() override;

        //! Creates a raster image for the given tile key
        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override;

    private:
        TMS::Driver _driver;
        void construct(std::string_view JSON, const IOOptions& io);
    };
}
