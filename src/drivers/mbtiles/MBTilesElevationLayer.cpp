/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/drivers/mbtiles/MBTilesElevationLayer.h>
#ifdef GEOSTUDIO_NATIVE_HAS_MBTILES

#include <geostudio/native/core/io/Context.h>
#include <geostudio/native/core/io/json.h>

using namespace geostudio::native;

GEOSTUDIO_NATIVE_ADD_OBJECT_FACTORY(MBTilesElevation,
    [](std::string_view JSON, const IOOptions& io) {
        return MBTilesElevationLayer::create(JSON, io); })


MBTilesElevationLayer::MBTilesElevationLayer() :
    super()
{
    construct({}, {});
}
MBTilesElevationLayer::MBTilesElevationLayer(std::string_view JSON, const IOOptions& io) :
    super(JSON, io)
{
    construct(JSON, io);
}

void
MBTilesElevationLayer::construct(std::string_view JSON, const IOOptions& io)
{
    setLayerTypeName("MBTilesElevation");
    const auto j = parse_json(JSON);
    get_to(j, "uri", uri, io);
    get_to(j, "format", format);
    get_to(j, "compress", compress);
}

std::string
MBTilesElevationLayer::to_json() const
{
    auto j = parse_json(super::to_json());
    set(j, "uri", uri);
    set(j, "format", format);
    set(j, "compress", compress);
    return j.dump();
}

Result<>
MBTilesElevationLayer::openImplementation(const IOOptions& io)
{
    auto parent = super::openImplementation(io);
    if (parent.failed())
        return parent;

    Profile new_profile = profile;
    DataExtentList dataExtents;

    auto r = _driver.open(
        name,
        *this, // MBTiles::Options
        false, // isWritingRequested()
        new_profile,
        dataExtents,
        io);

    if (r.failed())
        return r.error();

    // install the profile if there is one
    if (!profile.valid() && new_profile.valid())
    {
        profile = new_profile;
    }

    setDataExtents(dataExtents);

    return ResultVoidOK;
}

void
MBTilesElevationLayer::closeImplementation()
{
    _driver.close();
    super::closeImplementation();
}

Result<GeoImage>
MBTilesElevationLayer::createTileImplementation(const TileKey& key, const IOOptions& io) const
{
    if (status().failed())
        return status().error();

    auto result = _driver.read(key, io);

    if (result)
        return GeoImage(result.value(), key.extent());
    else
        return result.error();
}

#endif // GEOSTUDIO_NATIVE_HAS_MBTILES
