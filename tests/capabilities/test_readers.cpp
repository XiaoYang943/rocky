#include "../globe/catch.hpp"
#include <geostudio/native/drivers/gdal/GDALFeatureSource.h>
#include <geostudio/native/core/io/Context.h>
#include <geostudio/native/render/import/all/all.h>
#include <vsg/io/read.h>
#include <vsg/utils/ComputeBounds.h>
#include <filesystem>

using namespace geostudio::native;

TEST_CASE("GeoJSON fixture preserves polygon interior ring and attributes", "[drivers]")
{
    auto context = ContextFactory::create();
    auto source = GDALFeatureSource::create();
    source->uri = std::string(GN_FIXTURES) + "/polygon.geojson";
    REQUIRE(source->open());
    REQUIRE(source->featureCount() == 1);
    auto iterator = source->iterate(IOOptions{});
    REQUIRE(iterator.hasMore());
    auto feature = iterator.next();
    CHECK(feature.geometry.type == Geometry::Type::Polygon);
    REQUIRE(feature.geometry.parts.size() == 1);
    CHECK(feature.field("name").stringValue() == "parcel");
    CHECK(feature.field("count").intValue() == 7);
    CHECK_FALSE(iterator.hasMore());
}

TEST_CASE("GeoJSON empty collection reports its missing spatial extent", "[drivers]")
{
    auto context = ContextFactory::create();
    auto source = GDALFeatureSource::create();
    source->uri = std::string(GN_FIXTURES) + "/empty.geojson";
    auto result = source->open();
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("Invalid extent") != std::string::npos);
    CHECK(source->featureCount() == 0);
    source->close();
}

TEST_CASE("missing vector file reports failure and can be closed", "[drivers]")
{
    auto source = GDALFeatureSource::create();
    source->uri = std::string(GN_FIXTURES) + "/absent.geojson";
    CHECK_FALSE(source->open());
    source->close();
    source->close();
}

TEST_CASE("OBJ reader preserves triangle bounds without a device", "[readers]")
{
    auto options = vsg::Options::create();
    options->add(geostudio::native::io::all::create());
    auto node = vsg::read_cast<vsg::Node>(std::string(GN_FIXTURES) + "/triangle.obj", options);
    REQUIRE(node);
    vsg::ComputeBounds bounds;
    bounds.useNodeBounds = false;
    node->accept(bounds);
    REQUIRE(bounds.bounds.valid());
    CHECK(bounds.bounds.min.x == Approx(0));
    CHECK(bounds.bounds.max.x == Approx(2));
    CHECK(bounds.bounds.max.y == Approx(3));
}

TEST_CASE("PNG reader preserves fixture dimensions and channels", "[readers]")
{
    auto options = vsg::Options::create();
    options->add(geostudio::native::io::all::create());
    auto data = vsg::read_cast<vsg::Data>(std::string(GN_FIXTURES) + "/pixels.png", options);
    REQUIRE(data);
    CHECK(data->width() == 2);
    CHECK(data->height() == 1);
    REQUIRE(data->dataSize() == 8);
    const unsigned char expected[] = {255, 0, 0, 255, 0, 255, 0, 255};
    const auto pixels = static_cast<const unsigned char*>(data->dataPointer());
    for (unsigned index = 0; index < sizeof(expected); ++index)
        CHECK(pixels[index] == expected[index]);
}

TEST_CASE("reader dispatcher rejects missing unsupported input", "[readers]")
{
    auto options = vsg::Options::create();
    options->add(geostudio::native::io::all::create());
    CHECK_FALSE(vsg::read(std::string(GN_FIXTURES) + "/absent.unsupported", options));
}
