#include "../globe/catch.hpp"
#include <geostudio/native/core/io/Context.h>
#include <geostudio/native/drivers/tms/TMS.h>
#include <geostudio/native/drivers/gdal/GDALImageLayer.h>
#include <geostudio/native/drivers/gdal/GDALElevationLayer.h>
#include <geostudio/native/scene/layer/ElevationLayer.h>
#include <cstring>

using namespace geostudio::native;

static_assert(GEOSTUDIO_NATIVE_VERSION_AT_LEAST(0, 3, 0), "SDK version contract");
static_assert(!GEOSTUDIO_NATIVE_VERSION_BEFORE(0, 3, 0), "SDK version ordering");

namespace
{
    // 暴露纯 CPU 解码入口，不创建 Window、Device 或渲染系统。
    class ElevationProbe : public ElevationLayer
    {
    public:
        using ElevationLayer::decodeRGB;
    };
}

TEST_CASE("TMS and XYZ templates select opposite row origins", "[drivers][tiles]")
{
    Profile profile("spherical-mercator");
    TMS::TileMap map("https://host.test/{z}/{x}/{y}.png", profile, {}, "png", 256, 256);
    TileKey key(2, 1, 0, profile);
    CHECK(map.getURI(key, false) == "https://host.test/2/1/3.png");
    CHECK(map.getURI(key, true) == "https://host.test/2/1/0.png");
}

TEST_CASE("TMS negative row placeholder preserves inverted access", "[drivers][tiles]")
{
    Profile profile("spherical-mercator");
    TMS::TileMap map("https://host.test/${z}/${x}/${-y}.png", profile, {}, "png", 256, 256);
    CHECK(map.getURI(TileKey(2, 1, 0, profile), false) == "https://host.test/2/1/0.png");
}

TEST_CASE("Mapbox elevation reads RGB bytes without sRGB conversion", "[elevation]")
{
    ElevationProbe layer;
    layer.encoding = ElevationLayer::Encoding::MapboxRGB;
    for (auto format : {Image::R8G8B8_UNORM, Image::R8G8B8_SRGB})
    {
        auto image = Image::create(format, 2, 1);
        const unsigned char pixels[] = {1, 134, 160, 1, 138, 136};
        std::memcpy(image->data<unsigned char>(), pixels, sizeof(pixels));
        Heightfield result(layer.decodeRGB(image));
        CHECK(std::abs(result.heightAt(0, 0)) < 0.01f);
        CHECK(std::abs(result.heightAt(1, 0) - 100.0f) < 0.01f);
    }
}

TEST_CASE("Terrarium elevation retains fractional and negative heights", "[elevation]")
{
    ElevationProbe layer;
    layer.encoding = ElevationLayer::Encoding::TerrariumRGB;
    auto image = Image::create(Image::R8G8B8_UNORM, 2, 1);
    const unsigned char pixels[] = {128, 0, 128, 127, 255, 0};
    std::memcpy(image->data<unsigned char>(), pixels, sizeof(pixels));
    Heightfield result(layer.decodeRGB(image));
    CHECK(std::abs(result.heightAt(0, 0) - 0.5f) < 0.01f);
    CHECK(std::abs(result.heightAt(1, 0) + 1.0f) < 0.01f);
}

TEST_CASE("elevation decoder rejects empty images and retains float samples", "[elevation]")
{
    ElevationProbe layer;
    CHECK_FALSE(layer.decodeRGB(nullptr));
    CHECK_FALSE(layer.decodeRGB(Image::create()));
    Heightfield original(1, 1);
    original.heightAt(0, 0) = 123.5f;
    CHECK(layer.decodeRGB(original.image) == original.image);
}

TEST_CASE("GDAL raster layers report missing fixture and remain closed", "[drivers]")
{
    auto context = ContextFactory::create();
    auto image = GDALImageLayer::create();
    image->uri = std::string(GN_FIXTURES) + "/missing.tif";
    CHECK_FALSE(image->open(IOOptions{}));
    CHECK_FALSE(image->isOpen());
    image->close();
    auto elevation = GDALElevationLayer::create();
    elevation->uri = std::string(GN_FIXTURES) + "/missing.tif";
    CHECK_FALSE(elevation->open(IOOptions{}));
    CHECK_FALSE(elevation->isOpen());
    elevation->close();
}
