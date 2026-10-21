/**
 * rocky c++
 * Copyright Pelican Mapping
 * MIT License
 */
#include <geostudio/native/drivers/gdal/Geocoder.h>
#include <geostudio/native/data/vector/Feature.h>

#ifdef GEOSTUDIO_NATIVE_HAS_GEOCODER
#include <geostudio/native/drivers/gdal/GDALFeatureSource.h>
#include <ogr_geocoding.h>
#endif

using namespace geostudio::native;


Result<std::vector<Feature>>
Geocoder::geocode(const std::string& location, IOOptions& io) const
{
#ifdef GEOSTUDIO_NATIVE_HAS_GEOCODER

    std::vector<Feature> result;
    const char* options[2] = { "WRITE_CACHE=FALSE", nullptr };
    auto session = OGRGeocodeCreateSession((char**)options);
    if (session)
    {
        auto layerHandle = OGRGeocode(session, location.c_str(), nullptr, nullptr);
        if (layerHandle)
        {
            auto fs = GDALFeatureSource::create();
            fs->externalLayerHandle = layerHandle;
            fs->externalSRS = SRS::WGS84;
            fs->each(io, [&](Feature&& f)
                {
                    result.emplace_back(std::move(f));
                });
            OGRGeocodeFreeResult(layerHandle);
        }
        OGRGeocodeDestroySession(session);
    }

    if (result.empty())
        return Failure(Failure::ResourceUnavailable, "No results found");
    else
        return result;

#else
    return Failure(Failure::ServiceUnavailable, "Geocoder service is not available");
#endif
}
