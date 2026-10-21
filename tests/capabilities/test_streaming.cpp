#define CATCH_CONFIG_MAIN
#include "../globe/catch.hpp"
#include <geostudio/native/render/tileset/cesium/jsonUtils.h>
#include <geostudio/native/drivers/http/cesium/UrlAssetAccessor.h>
#include <CesiumAsync/ITaskProcessor.h>
#include <CesiumAsync/IAssetRequest.h>
#include <CesiumAsync/IAssetResponse.h>
#include <filesystem>
#include <fstream>

using namespace geostudio::native::tiles3d;

namespace
{
    // 测试线程内执行任务，验证资源协议；不依赖线程时序或窗口。
    struct InlineTasks : CesiumAsync::ITaskProcessor
    {
        void startTask(std::function<void()> task) override { task(); }
    };

    bool recognize(std::string source)
    {
        return isTilesetJson(std::span<char>(source.data(), source.size()));
    }
}

TEST_CASE("tileset recognizer accepts supported asset version strings", "[streaming]")
{
    CHECK(recognize(R"({"asset":{"version":"1.0"},"root":{}})"));
    CHECK(recognize(R"({"asset":{"version":"1.1"},"root":{}})"));
}

TEST_CASE("tileset recognizer rejects truncated JSON", "[streaming]")
{
    CHECK_FALSE(recognize("{\"asset\":"));
    CHECK_FALSE(recognize(""));
}

TEST_CASE("tileset recognizer requires an asset version", "[streaming]")
{
    CHECK_FALSE(recognize("{}"));
    CHECK_FALSE(recognize(R"({"asset":{}})"));
    CHECK_FALSE(recognize(R"({"asset":{"version":7}})"));
    CHECK_FALSE(recognize(R"({"asset":"1.0"})"));
}

TEST_CASE("required JSON string preserves content URI", "[streaming]")
{
    rapidjson::Document json;
    json.Parse(R"({"uri":"content/tile + 1.glb"})");
    CHECK(getStringOrError(json, "uri", "missing uri") == "content/tile + 1.glb");
}

TEST_CASE("required JSON string rejects missing and numeric values", "[streaming]")
{
    rapidjson::Document json;
    json.Parse(R"({"uri":42})");
    CHECK_THROWS_AS(getStringOrError(json, "uri", "wrong type"), std::runtime_error);
    CHECK_THROWS_AS(getStringOrError(json, "absent", "missing"), std::runtime_error);
}

TEST_CASE("JSON parser does not initialize a destination on malformed input", "[streaming]")
{
    struct Destination
    {
        bool initialized = false;
        void init(const rapidjson::Value&) { initialized = true; }
    } value;
    CHECK_THROWS_AS(initFromJSON(value, std::string("{")), std::runtime_error);
    CHECK_FALSE(value.initialized);
    initFromJSON(value, std::string("{}"));
    CHECK(value.initialized);
}

TEST_CASE("asset accessor returns exact local fixture bytes", "[streaming][assets]")
{
    CesiumAsync::AsyncSystem async(std::make_shared<InlineTasks>());
    UrlAssetAccessor accessor;
    const std::string path = std::string(GN_FIXTURES) + "/polygon.geojson";
    auto result = accessor.get(async, "file:///" + path, {}).wait();
    REQUIRE(result);
    REQUIRE(result->response());
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    std::string expected((std::istreambuf_iterator<char>(input)), {});
    const auto bytes = result->response()->data();
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == expected);
    CHECK(result->method() == "GET");
}

TEST_CASE("asset accessor reports a missing file and recovers on next request", "[streaming][assets]")
{
    CesiumAsync::AsyncSystem async(std::make_shared<InlineTasks>());
    UrlAssetAccessor accessor;
    CHECK_THROWS(accessor.get(async, "file:///" + std::string(GN_FIXTURES) + "/missing.glb", {}).wait());
    auto result = accessor.get(async, "file:///" + std::string(GN_FIXTURES) + "/empty.geojson", {}).wait();
    REQUIRE(result->response());
    CHECK_FALSE(result->response()->data().empty());
}
