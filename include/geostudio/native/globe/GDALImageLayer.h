/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/GDAL.h>
#ifdef ROCKY_HAS_GDAL

#include <geostudio/native/globe/ImageLayer.h>

namespace geostudio::native
{
    /**
     * Image layer connected to a GDAL raster dataset
     */
    class ROCKY_EXPORT GDALImageLayer : public Inherit<ImageLayer, GDALImageLayer>,
        public GDAL_detail::Options
    {
    public:
        //! Construct a GDAL image layer
        GDALImageLayer();

        //! Deserialize a GDAL image layer
        GDALImageLayer(std::string_view JSON, const IOOptions& io);

        //! Serialize the layer
        std::string to_json() const override;

    protected: // Layer

        //! Establishes a connection to the GDAL data source
        Result<> openImplementation(const IOOptions&) override;

        //! Closes down any GDAL connections
        void closeImplementation() override;

        //! Gets a raster image for the given tile key
        Result<GeoImage> createTileImplementation(
            const TileKey& key,
            const IOOptions& io) const override;

    private:

        //! Called by the constructors
        void construct(std::string_view JSON, const IOOptions& io);

        mutable detail::ThreadLocal<GDAL_detail::Driver> _drivers;
        friend class GDAL_detail::Driver;
    };

} // namespace geostudio::native

#endif // ROCKY_HAS_GDAL
