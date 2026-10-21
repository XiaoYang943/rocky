#define CATCH_CONFIG_MAIN
#include "../globe/catch.hpp"
#include <geostudio/native/core/base/Callbacks.h>
#include <geostudio/native/core/cache/Cache.h>
#include <geostudio/native/data/vector/Feature.h>
#include <geostudio/native/data/raster/GeoImage.h>
#include <geostudio/native/scene/map/Map.h>
#include <geostudio/native/core/geo/tiling/TileKey.h>
#include <geostudio/native/core/io/URI.h>
#include <thread>

using namespace geostudio::native;

namespace
{
    class UnitLayer : public Inherit<Layer, UnitLayer>
    {
    public:
        UnitLayer() = default;
    };
}

TEST_CASE("callback releases an abandoned subscriber", "[core]")
{
    Callback<int> event;
    int total = 0;
    auto subscription = event([&](int value) { total += value; });
    event.fire(3);
    REQUIRE(total == 3);
    subscription.reset();
    event.fire(10);
    CHECK(total == 3);
}

TEST_CASE("callback keeps independent subscriptions alive", "[core]")
{
    Callback<> event;
    int a = 0, b = 0;
    auto first = event([&] { ++a; });
    auto second = event([&] { ++b; });
    first.reset();
    event.fire();
    CHECK(a == 0);
    CHECK(b == 1);
}

TEST_CASE("resident cache does not own the cached resource", "[core]")
{
    detail::ResidentCache<int, int> cache;
    auto item = std::make_shared<int>(42);
    cache.put(1, item, true);
    REQUIRE(cache.get(1));
    CHECK(*cache.get(1)->first == 42);
    item.reset();
    CHECK_FALSE(cache.get(1));
}

TEST_CASE("resident cache isolates keys and clears statistics", "[core]")
{
    detail::ResidentCache<int, int> cache;
    auto value = std::make_shared<int>(7);
    cache.put(4, value, true);
    CHECK_FALSE(cache.get(5));
    REQUIRE(cache.get(4));
    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(cache.hits() == 0);
    CHECK(cache.misses() == 0);
}

TEST_CASE("LRU access protects the recently used item", "[core]")
{
    detail::LRUCache<int, int> cache(2);
    cache.put(1, 10); cache.put(2, 20);
    REQUIRE(cache.get(1) == 10);
    cache.put(3, 30);
    CHECK_FALSE(cache.get(2));
    CHECK(cache.get(1) == 10);
    CHECK(cache.get(3) == 30);
}

TEST_CASE("LRU replaces an existing key without growing", "[core]")
{
    detail::LRUCache<int, int> cache(2);
    cache.put(1, 10); cache.put(1, 11);
    CHECK(cache.size() == 1);
    CHECK(cache.get(1) == 11);
}

TEST_CASE("geographic to ECEF preserves height on round trip", "[geo]")
{
    auto forward = SRS::WGS84.to(SRS::ECEF);
    for (auto point : {glm::dvec3(108.0, 34.0, 123.0), glm::dvec3(-179.0, -70.0, 0.0)})
    {
        glm::dvec3 world, restored;
        REQUIRE(forward.transform(point, world));
        REQUIRE(forward.inverse(world, restored));
        CHECK(glm::length(restored - point) < 1e-5);
    }
}

TEST_CASE("web mercator preserves longitude latitude axis order", "[geo]")
{
    glm::dvec3 output;
    REQUIRE(SRS::WGS84.to(SRS("epsg:3857")).transform(glm::dvec3(90.0, 0.0, 0.0), output));
    CHECK(output.x == Approx(10018754.1713946));
    CHECK(std::abs(output.y) < 1e-7);
}

TEST_CASE("tile children cover their parent extent", "[tiles]")
{
    TileKey parent(3, 4, 2, Profile("global-geodetic"));
    double area = 0.0;
    for (unsigned q = 0; q != 4; ++q)
    {
        auto child = parent.createChildKey(q);
        CHECK(child.createParentKey() == parent);
        CHECK(child.getQuadrant() == q);
        const auto extent = child.extent();
        area += extent.width() * extent.height();
    }
    CHECK(area == Approx(parent.extent().width() * parent.extent().height()));
}

TEST_CASE("tile root has no parent", "[tiles]")
{
    CHECK_FALSE(TileKey(0, 0, 0, Profile("global-geodetic")).createParentKey().valid());
}

TEST_CASE("tile point lookup follows north to south row order", "[tiles]")
{
    Profile profile("global-geodetic");
    auto north = TileKey::createTileKeyContainingPoint(100, 45, 4, profile);
    auto south = TileKey::createTileKeyContainingPoint(100, -45, 4, profile);
    CHECK(north.x == south.x);
    CHECK(north.y < south.y);
    CHECK(north.extent().contains(100, 45));
}

TEST_CASE("geographic extent contains both sides of the dateline", "[geo]")
{
    GeoExtent extent(SRS::WGS84, 170, -10, 190, 10);
    CHECK(extent.contains(175, 0));
    CHECK(extent.contains(-175, 0));
    CHECK_FALSE(extent.contains(0, 0));
}

TEST_CASE("heightfield interpolation preserves an affine plane", "[elevation]")
{
    Heightfield height(3, 3);
    for (unsigned y = 0; y != 3; ++y)
        for (unsigned x = 0; x != 3; ++x)
            height.heightAt(x, y) = 10.0f * x + 20.0f * y;
    CHECK(height.heightAtUV(0.5f, 0.5f) == Approx(30.0f));
    CHECK(height.heightAtUV(0, 0) == Approx(0.0f));
    CHECK(height.heightAtUV(1, 1) == Approx(60.0f));
}

TEST_CASE("encoded heights retain valid extrema and missing samples", "[elevation]")
{
    Heightfield height(3, 1);
    height.heightAt(0, 0) = -50.0f;
    height.heightAt(1, 0) = 500.0f;
    height.heightAt(2, 0) = NO_DATA_VALUE;
    height.computeAndSetMinMax();
    const auto encoded = height.encode();
    CHECK(std::abs(encoded.heightAt(0, 0) + 50.0f) < 0.02f);
    CHECK(std::abs(encoded.heightAt(1, 0) - 500.0f) < 0.02f);
    CHECK(encoded.heightAt(2, 0) == NO_DATA_VALUE);
}

TEST_CASE("feature fields preserve integer precision and missing state", "[vector]")
{
    Feature feature;
    feature.fields["id"].emplace<std::int64_t>(9007199254740993LL);
    CHECK(feature.field("id").intValue() == 9007199254740993LL);
    CHECK_FALSE(feature.field("absent").valid());
}

TEST_CASE("URI resolves relative content beside its tileset", "[io]")
{
    CHECK(URI("tile.glb", "https://host.test/tiles/tileset.json").full() == "https://host.test/tiles/tile.glb");
    CHECK(URI("tile.glb", "https://host.test/tiles/").full() == "https://host.test/tiles/tile.glb");
}

TEST_CASE("map layer snapshots remain valid after replacement", "[scene]")
{
    auto map = Map::create();
    auto first = UnitLayer::create();
    map->add(first);
    auto snapshot = map->layers();
    auto revision = map->revision();
    map->setLayers(Map::Layers{});
    CHECK(map->layers().empty());
    CHECK(snapshot.size() == 1);
    CHECK(snapshot.front() == first);
    CHECK(map->revision() > revision);
}

TEST_CASE("map subscription observes replacement and can detach", "[scene]")
{
    auto map = Map::create();
    int notifications = 0;
    auto sub = map->onLayersChanged([&](const Map*) { ++notifications; });
    map->add(UnitLayer::create());
    REQUIRE(notifications == 1);
    sub.reset();
    map->setLayers(Map::Layers{});
    CHECK(notifications == 1);
}
