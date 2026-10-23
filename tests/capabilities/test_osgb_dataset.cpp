#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBDataset.h"
#include "../../src/render/import/osgb/OSGBFiles.h"
#include <geostudio/native/render/import/osgb/OSGBPaging.h>
#include <vsg/all.h>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace geostudio::native::io;
using namespace geostudio::native::io::detail;
namespace fs = std::filesystem;
namespace
{
const std::string metadataText = "<ModelMetadata><SRS>EPSG:4545</SRS><SRSOrigin>607429,3938520,0</SRSOrigin></ModelMetadata>";
fs::path base() { return fs::u8path(GN_OSGB_FIXTURES) / "dataset-tests"; }
void ownedFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    if (fs::exists(path)) REQUIRE(readBoundedFile(path, 1024 * 1024) == content);
    else { std::ofstream output(path, std::ios::binary); output << content; REQUIRE(output.good()); }
}
struct Fixture
{
    DatasetIndex index;
    DatasetBuildSettings settings;
    uint64_t calls = 0;
    explicit Fixture(const std::string& name, size_t roots = 3)
    {
        index.sourceRoot = base() / fs::u8path(name) / u8"原始数据";
        index.srs = "EPSG:4545"; index.origin = {607429, 3938520, 0};
        index.coordinatePolicy = "dataset-cpu-test"; index.metadataContents = metadataText;
        ownedFile(index.sourceRoot / "metadata.xml", metadataText);
        ownedFile(index.sourceRoot / "Textures" / "color.bin", "texture-source");
        for (size_t i = 0; i < roots; ++i)
        {
            RootRecord root;
            const auto id = std::to_string(i);
            root.relativePath = "Data/Tile_" + id + "/Tile_" + id + ".osgb";
            ownedFile(index.sourceRoot / fs::u8path(root.relativePath), "source-root-" + id);
            ownedFile(index.sourceRoot / "Data" / fs::u8path("Tile_" + id) / "detail.osgb", "source-detail-" + id);
            root.localEcefSphere = std::array<double, 4>{double(i) * 100.0, 0.0, 0.0, 10.0};
            index.roots.push_back(root);
        }
        settings.outputDirectory = base() / fs::u8path(name) / "derived";
        settings.conversionIdentity = "dataset-test-converter-v1";
        settings.overviewIdentity = "dataset-test-overview-v1";
        settings.rootsPerRegion = 2;
    }
    static vsg::ref_ptr<vsg::Node> geometry(double x)
    {
        auto data = vsg::vec3Array::create(3);
        (*data)[0] = vsg::vec3(float(x), 0.0f, 0.0f);
        (*data)[1] = vsg::vec3(float(x) + 1.0f, 0.0f, 0.0f);
        (*data)[2] = vsg::vec3(float(x), 1.0f, 0.0f);
        auto draw = vsg::VertexDraw::create(); draw->assignArrays(vsg::DataList{data});
        draw->vertexCount = 3; draw->instanceCount = 1;
        return draw;
    }
    PreparedRoot prepare(const RootRecord& root, uint64_t)
    {
        ++calls;
        const auto& s = *root.localEcefSphere;
        PreparedRoot result;
        result.bound = vsg::dsphere(s[0], s[1], s[2], s[3]);
        auto page = vsg::PagedLOD::create();
        page->bound = result.bound;
        page->filename = vsg::Path((index.sourceRoot / fs::u8path(root.relativePath).parent_path() / "detail.osgb").wstring());
        page->children[0].minimumScreenHeightRatio = 0.1;
        page->children[1].node = geometry(s[0]);
        page->children[1].minimumScreenHeightRatio = 0.0;
        result.full = page; result.overview = geometry(s[0]);
        result.inputTriangles = 1; result.outputTriangles = 1;
        const auto texture = index.sourceRoot / "Textures" / "color.bin";
        FileIdentity identity; identity.size = fs::file_size(texture); identity.sha256 = sha256File(texture);
        result.sourceDependencies.push_back({"Textures/color.bin", identity});
        return result;
    }
    DatasetBuildResult build(const DatasetCancelled& cancelled = {})
    { return buildDataset(index, settings, [&](const auto& root, auto bytes) { return prepare(root, bytes); }, cancelled); }
};
}

TEST_CASE("OSGB dataset publishes bounded resident regions and restores current paging options", "[osgb-dataset]")
{
    Fixture fixture("resident-regions", 17);
    fixture.settings.rootsPerRegion = 16;
    auto built = fixture.build();
    REQUIRE(built.roots == 17);
    REQUIRE(built.regions == 2);
    REQUIRE(built.outputBytes > 0);
    auto manifest = inspectManifest(built.manifestPath, fixture.settings.conversionIdentity);
    CHECK(manifest.bound.r > 800.0);
    auto token = OSGBPagingToken::create();
    auto options = vsg::Options::create(); options->setObject(osgbPagingTokenKey, token);
    auto group = loadOverviewGroup(manifest, options).cast<vsg::Group>();
    REQUIRE(group);
    REQUIRE(group->children.size() == 2);
    auto region = group->children[0].cast<vsg::PagedLOD>();
    REQUIRE(region);
    REQUIRE(region->children[1].node);
    CHECK_FALSE(region->children[0].node);
    CHECK(fs::u8path(region->filename.string()).extension() == ".osgbregion");
    CHECK(region->options->getObject<OSGBPagingToken>(osgbPagingTokenKey) == token.get());
    auto full = readRegion(fs::path(region->filename.wstring()), fixture.settings.conversionIdentity, {}, options).cast<vsg::Group>();
    REQUIRE(full);
    REQUIRE(full->children.size() == 16);
    auto child = full->children[0].cast<vsg::PagedLOD>();
    REQUIRE(child);
    CHECK(child->children[1].node);
    CHECK(fs::is_regular_file(fs::path(child->filename.wstring())));
    CHECK(pathWithin(fs::path(child->filename.wstring()), fixture.index.sourceRoot));
    CHECK(child->options->getObject<OSGBPagingToken>(osgbPagingTokenKey) == token.get());
    auto other = OSGBPagingToken::create();
    auto otherOptions = vsg::Options::create(); otherOptions->setObject(osgbPagingTokenKey, other);
    auto independentlyRead = readRegion(fs::path(region->filename.wstring()), fixture.settings.conversionIdentity, {}, otherOptions).cast<vsg::Group>();
    REQUIRE(independentlyRead);
    auto secondChild = independentlyRead->children[0].cast<vsg::PagedLOD>();
    REQUIRE(secondChild);
    CHECK(secondChild != child);
    CHECK(secondChild->options->getObject<OSGBPagingToken>(osgbPagingTokenKey) == other.get());
    Fixture relocated("resident-relocated", 17);
    auto relocatedFull = readRegion(fs::path(region->filename.wstring()), fixture.settings.conversionIdentity,
        relocated.index.sourceRoot, otherOptions).cast<vsg::Group>();
    REQUIRE(relocatedFull);
    auto relocatedChild = relocatedFull->children[0].cast<vsg::PagedLOD>();
    REQUIRE(relocatedChild);
    CHECK(pathWithin(fs::path(relocatedChild->filename.wstring()), relocated.index.sourceRoot));
}

TEST_CASE("OSGB dataset reuse validates roots textures metadata and all output bytes", "[osgb-dataset]")
{
    Fixture fixture("reuse-identities");
    auto first = fixture.build();
    const auto stamp = fs::last_write_time(first.manifestPath);
    fixture.calls = 0;
    const auto reused = fixture.build();
    CHECK(reused.reused);
    CHECK(fixture.calls == 0);
    CHECK(fs::last_write_time(first.manifestPath) == stamp);
    CHECK_THROWS(inspectManifest(first.manifestPath, "different-converter"));
    auto manifest = inspectManifest(first.manifestPath, fixture.settings.conversionIdentity);
    const auto dependency = fixture.index.sourceRoot / "Textures" / "color.bin";
    // 此文件只属于本 fixture，恢复写入不涉及用户原件；同长度修改也必须被 SHA 拒绝。
    { std::ofstream changed(dependency, std::ios::binary); changed << "texture-mutant"; }
    CHECK_THROWS(loadOverviewGroup(manifest));
    { std::ofstream restored(dependency, std::ios::binary); restored << "texture-source"; }
    REQUIRE(loadOverviewGroup(manifest));
    const auto fullPath = fixture.settings.outputDirectory / fs::u8path(manifest.regions.front().full);
    const auto original = readBoundedFile(fullPath, 1024 * 1024);
    auto damaged = original; damaged.back() ^= 1;
    { std::ofstream changed(fullPath, std::ios::binary); changed.write(damaged.data(), damaged.size()); }
    CHECK_THROWS(readRegion(fixture.settings.outputDirectory / fs::u8path(manifest.regions.front().entry), fixture.settings.conversionIdentity));
    { std::ofstream restored(fullPath, std::ios::binary); restored.write(original.data(), original.size()); }
    const auto metadataPath = fixture.index.sourceRoot / "metadata.xml";
    { std::ofstream changed(metadataPath, std::ios::binary); changed << metadataText << " "; }
    CHECK_THROWS(inspectManifest(first.manifestPath, fixture.settings.conversionIdentity));
    { std::ofstream restored(metadataPath, std::ios::binary); restored << metadataText; }
    fixture.settings.overviewIdentity = "dataset-test-overview-v2";
    fixture.calls = 0;
    auto changed = fixture.build();
    CHECK_FALSE(changed.reused);
    CHECK(fixture.calls == 3);
}

TEST_CASE("OSGB dataset rejects runtime placeholders missing children and budget overflow", "[osgb-dataset]")
{
    Fixture fixture("invalid-callback", 1);
    auto empty = [&](const RootRecord&, uint64_t) {
        PreparedRoot result; result.bound = vsg::dsphere(0.0, 0.0, 0.0, 10.0);
        result.full = vsg::Group::create(); result.overview = Fixture::geometry(0.0); return result;
    };
    CHECK_THROWS(buildDataset(fixture.index, fixture.settings, empty));
    auto zeroDraw = [&](const RootRecord& root, uint64_t remaining) {
        auto result = fixture.prepare(root, remaining); result.full = vsg::VertexDraw::create(); return result;
    };
    CHECK_THROWS(buildDataset(fixture.index, fixture.settings, zeroDraw));
    fixture.settings.maxWorkingBytes = 1;
    CHECK_THROWS(fixture.build());
    fixture.settings.maxWorkingBytes = 1024 * 1024;
    fixture.settings.maxOutputBytes = 2ull * 1024 * 1024 * 1024 + 1;
    CHECK_THROWS(fixture.build());
    fixture.settings.maxOutputBytes = 2ull * 1024 * 1024 * 1024;
    fixture.settings.outputDirectory = fixture.index.sourceRoot / "forbidden";
    CHECK_THROWS(fixture.build());
    fixture.settings.outputDirectory = base() / "invalid-callback" / "derived";
    auto broken = [&](const RootRecord& root, uint64_t remaining) {
        auto result = fixture.prepare(root, remaining);
        result.full.cast<vsg::PagedLOD>()->filename = "missing.osgb";
        return result;
    };
    CHECK_THROWS(buildDataset(fixture.index, fixture.settings, broken));
}

TEST_CASE("OSGB dataset cancellation preserves the previous valid manifest", "[osgb-dataset]")
{
    Fixture fixture("cancel-preserves");
    auto first = fixture.build();
    const auto before = sha256File(first.manifestPath);
    fixture.settings.overviewIdentity = "cancelled-quality";
    fixture.calls = 0;
    const auto cancelled = [&] { return fixture.calls >= 1; };
    CHECK_THROWS(fixture.build(cancelled));
    CHECK(sha256File(first.manifestPath) == before);
    auto manifest = inspectManifest(first.manifestPath, fixture.settings.conversionIdentity);
    REQUIRE(loadOverviewGroup(manifest));
}

TEST_CASE("OSGB dataset planning refuses unknown bounds and escaped child references", "[osgb-dataset]")
{
    Fixture fixture("path-boundaries", 1);
    fixture.index.roots[0].localEcefSphere.reset();
    CHECK_THROWS(fixture.build());
    fixture.index.roots[0].localEcefSphere = std::array<double, 4>{0.0, 0.0, 0.0, 10.0};
    ownedFile(base() / "path-boundaries" / "outside.osgb", "outside");
    auto escaped = [&](const RootRecord& root, uint64_t remaining) {
        auto result = fixture.prepare(root, remaining);
        result.full.cast<vsg::PagedLOD>()->filename = vsg::Path((base() / "path-boundaries" / "outside.osgb").wstring());
        return result;
    };
    CHECK_THROWS(buildDataset(fixture.index, fixture.settings, escaped));
}
