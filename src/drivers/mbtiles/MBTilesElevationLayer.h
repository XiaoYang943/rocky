/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/drivers/mbtiles/MBTiles.h>
#ifdef GEOSTUDIO_NATIVE_HAS_MBTILES

#include <geostudio/native/scene/layer/ElevationLayer.h>

namespace geostudio::native
{
    /**
     * Elevation layer reading from an MBTiles database.
     * GDAL support MBTiles directly, but this is an extended implementation
     * that supports SRS other than spherical mercator, customizable image
     * formats, and tile compression.
     */
    class GEOSTUDIO_NATIVE_API MBTilesElevationLayer : public Inherit<ElevationLayer, MBTilesElevationLayer>,
        public MBTiles::Options
    {
    public:
        //! Construct an empty layer
        MBTilesElevationLayer();
        explicit MBTilesElevationLayer(std::string_view JSON, const IOOptions& io);

        //! serialize
        std::string to_json() const override;

    protected:

        //! Creates a raster image for the given tile key
        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override;

        //! Opens the layer and returns its status
        Result<> openImplementation(const IOOptions& io) override;

        //! Closes the layer and returns its status
        void closeImplementation() override;

    private:
        MBTiles::Driver _driver;
        void construct(std::string_view JSON, const IOOptions& io);
    };
}

#endif // GEOSTUDIO_NATIVE_HAS_MBTILES
