#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBArtifacts.h"
#include "../../src/render/import/osgb/OSGBResources.h"
#include "../../src/render/import/osgb/OSGBSerialization.h"
#include "../../src/render/import/osgb/OSGBFiles.h"
#include <vsg/all.h>
#include <nlohmann/json.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <thread>

using namespace geostudio::native::io::detail;
namespace fs = std::filesystem;
namespace
{
fs::path base() { return fs::u8path(GN_OSGB_FIXTURES) / "artifacts"; }
// 输入仅为固定小型身份样本，不解析真实数据；既有样本必须内容一致。
void fixture(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    if (fs::exists(path)) REQUIRE(readBoundedFile(path, 1024 * 1024) == content);
    else
    {
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        REQUIRE(file.good());
    }
}
fs::path source(const std::string& name = "source")
{
    const auto path = base() / fs::u8path(name);
    fixture(path / "Tile" / "root.osgb", "root-source-v1");
    fixture(path / "Tile" / "texture.png", "external-image-v1");
    return path;
}
ArtifactContext context(const std::string& name)
{
    ArtifactContext result;
    result.sourceRoot = source();
    result.cacheRoot = base() / "cache" / name;
    result.coordinateIdentity = "srs=local;origin=0,0,0;anchor=0,0,0;lod=pixel-v1";
    result.generatorIdentity = "test-generator-v1";
    result.maxCacheBytes = 1024 * 1024;
    result.maxEntryBytes = 128 * 1024;
    return result;
}
ArtifactPage input(const ArtifactContext& settings)
{
    ArtifactPage result;
    result.sourceFile = settings.sourceRoot / "Tile" / "root.osgb";
    result.externalImages.push_back(settings.sourceRoot / "Tile" / "texture.png");
    result.expectedSourceSha256 = sha256File(result.sourceFile);
    result.expectedImageHashes[result.externalImages.front()] = sha256File(result.externalImages.front());
    return result;
}
struct Page
{
    vsg::ref_ptr<vsg::PagedLOD> node;
    vsg::ref_ptr<vsg::DescriptorImage> image;
};
Page page(const ArtifactContext& settings, vsg::ref_ptr<vsg::Data> data = {})
{
    Page result;
    result.node = vsg::PagedLOD::create();
    result.node->filename = (settings.sourceRoot / "Tile" / "child.osgb").u8string();
    result.node->bound = vsg::dsphere(1.0, 2.0, 3.0, 9.0);
    result.node->setValue("osgb.pixelSplit", 123.0);
    result.node->children[0].minimumScreenHeightRatio = 0.25;
    result.node->children[1].minimumScreenHeightRatio = 0;
    auto state = vsg::StateGroup::create();
    if (!data)
    {
        auto rgba = vsg::ubvec4Array2D::create(2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
        for (auto& value : *rgba) value = vsg::ubvec4(20, 40, 80, 73);
        data = rgba;
    }
    auto sampler = vsg::Sampler::create();
    result.image = vsg::DescriptorImage::create(sampler, data, 0);
    auto descriptorLayout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorLayout}, vsg::PushConstantRanges{});
    auto descriptors = vsg::DescriptorSet::create(descriptorLayout, vsg::Descriptors{result.image});
    state->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptors));
    auto triangle = vsg::VertexIndexDraw::create();
    auto vertices = vsg::vec3Array::create(3);
    (*vertices)[0] = {0, 0, 0}; (*vertices)[1] = {1, 0, 0}; (*vertices)[2] = {0, 1, 0};
    triangle->assignArrays(vsg::DataList{vertices});
    triangle->assignIndices(vsg::ushortArray::create({0, 1, 2}));
    triangle->indexCount = 3; triangle->instanceCount = 1;
    state->addChild(triangle);
    result.node->children[1].node = state;
    return result;
}
struct FindImage : vsg::Visitor
{
    vsg::ref_ptr<vsg::DescriptorImage> image;
    void apply(vsg::Object& object) override { object.traverse(*this); }
    void apply(vsg::DescriptorImage& value) override { image = &value; }
};
vsg::ref_ptr<vsg::Data> imageData(vsg::Node& node)
{
    FindImage find;
    node.accept(find);
    REQUIRE(find.image);
    return find.image->imageInfoList.front()->imageView->image->data;
}
void stored(const OSGBArtifacts::StoreResult& result)
{
    INFO(result.diagnostic);
    REQUIRE((result.state == OSGBArtifacts::StoreState::Stored || result.state == OSGBArtifacts::StoreState::AlreadyPresent));
}
// 只临时修改本测试已校验的受管样本/缓存文件，析构恢复；不删除文件或追加副本。
struct RestoreFile
{
    fs::path path;
    std::string original;
    explicit RestoreFile(fs::path value) : path(std::move(value)), original(readBoundedFile(path, 1024 * 1024)) {}
    void write(const std::string& content)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file.good()) throw std::runtime_error("Cannot update managed artifact test fixture");
    }
    ~RestoreFile() { try { write(original); } catch (...) {} }
};
fs::path manifest(const ArtifactContext& settings)
{
    return settings.cacheRoot / (sha256Bytes("Tile/root.osgb") + ".page.json");
}
}

TEST_CASE("OSGB artifacts restore paging paths and current reader options", "[osgb][artifacts]")
{
    auto settings = context("roundtrip");
    OSGBArtifacts cache(settings);
    auto sample = page(settings);
    const auto filename = sample.node->filename;
    const auto originalData = imageData(*sample.node);
    stored(cache.store(input(settings), *sample.node));
    CHECK(sample.node->filename == filename);
    CHECK(imageData(*sample.node) == originalData);
    auto options = vsg::Options::create();
    options->setValue("reader-marker", 42);
    auto hit = cache.load(input(settings).sourceFile, options);
    INFO(hit.diagnostic);
    REQUIRE(hit.state == OSGBArtifacts::LookupState::Hit);
    auto restored = hit.node.cast<vsg::PagedLOD>();
    REQUIRE(restored);
    CHECK(fs::weakly_canonical(fs::u8path(restored->filename.string())) == fs::weakly_canonical(fs::u8path(filename.string())));
    CHECK_FALSE(restored->children[0].node);
    CHECK(restored->children[0].minimumScreenHeightRatio == 0.25);
    CHECK(restored->bound.center.x == 1);
    double split = 0;
    CHECK(restored->getValue("osgb.pixelSplit", split)); CHECK(split == 123);
    int marker = 0;
    REQUIRE(restored->options);
    CHECK(restored->options->getValue("reader-marker", marker)); CHECK(marker == 42);
    CHECK_FALSE(fs::exists(settings.sourceRoot / "Tile" / "child.osgb"));
    CHECK(std::memcmp(imageData(*restored)->dataPointer(), originalData->dataPointer(), originalData->dataSize()) == 0);

    auto relocated = settings;
    relocated.sourceRoot = source(u8"迁移 数据");
    OSGBArtifacts moved(relocated);
    auto movedHit = moved.load(input(relocated).sourceFile);
    INFO(movedHit.diagnostic);
    REQUIRE(movedHit.state == OSGBArtifacts::LookupState::Hit);
    CHECK(fs::weakly_canonical(fs::u8path(movedHit.node.cast<vsg::PagedLOD>()->filename.string())) ==
        fs::weakly_canonical(relocated.sourceRoot / "Tile" / "child.osgb"));
    CHECK(cache.statistics().hits == 1);
}

TEST_CASE("OSGB artifacts preserve processed RGB RGBA and compressed mip data", "[osgb][artifacts]")
{
    for (unsigned variant = 0; variant != 4; ++variant)
    {
        auto settings = context("processed-" + std::to_string(variant));
        vsg::ref_ptr<vsg::Data> data;
        if (variant == 0)
        {
            data = vsg::ubvec3Array2D::create(2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8_UNORM});
            std::memset(data->dataPointer(), 31, data->dataSize());
        }
        else if (variant >= 2)
        {
            vsg::Data::Properties properties{variant == 2 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK};
            properties.blockWidth = properties.blockHeight = 4;
            properties.mipLevels = 3;
            auto storage = vsg::ubyteArray::create(21 * (variant == 2 ? 8 : 16));
            std::memset(storage->dataPointer(), 0x5a, storage->dataSize());
            if (variant == 2) data = vsg::block64Array2D::create(storage, 0, 8, 4, 4, properties);
            else data = vsg::block128Array2D::create(storage, 0, 16, 4, 4, properties);
        }
        auto sample = page(settings, data);
        OSGBResources resources;
        resources.process(*sample.node);
        const auto expected = imageData(*sample.node);
        OSGBArtifacts cache(settings);
        stored(cache.store(input(settings), *sample.node));
        CHECK(imageData(*sample.node) == expected);
        auto hit = cache.load(input(settings).sourceFile);
        INFO(hit.diagnostic);
        REQUIRE(hit.state == OSGBArtifacts::LookupState::Hit);
        auto actual = imageData(*hit.node);
        CHECK(actual->properties.format == expected->properties.format);
        CHECK(actual->properties.mipLevels == expected->properties.mipLevels);
        REQUIRE(actual->dataSize() == expected->dataSize());
        CHECK(std::memcmp(actual->dataPointer(), expected->dataPointer(), expected->dataSize()) == 0);
        resources.process(*hit.node);
        CHECK(imageData(*hit.node)->properties.format == expected->properties.format);
    }
}

TEST_CASE("OSGB artifact identities reject changed input and settings", "[osgb][artifacts]")
{
    const auto settings = context("identities");
    OSGBArtifacts cache(settings);
    auto sample = page(settings);
    const auto observed = input(settings);
    stored(cache.store(observed, *sample.node));
    for (bool texture : {false, true})
    {
        RestoreFile restore(texture ? observed.externalImages.front() : observed.sourceFile);
        restore.write(texture ? "external-image-v2" : "root-source-v2");
        auto stale = cache.load(observed.sourceFile);
        CHECK(stale.state == OSGBArtifacts::LookupState::Invalid);
        CHECK_FALSE(stale.diagnostic.empty());
        auto write = cache.store(observed, *sample.node);
        CHECK(write.state == OSGBArtifacts::StoreState::Failed);
        CHECK(write.diagnostic.find("changed after decode") != std::string::npos);
    }
    for (unsigned variant = 0; variant != 3; ++variant)
    {
        auto changed = settings;
        if (variant == 0) changed.coordinateIdentity += ";anchor=changed";
        if (variant == 1) changed.generatorIdentity += ";new-converter";
        if (variant == 2) changed.colorTexturesSRGB = false;
        OSGBArtifacts other(changed);
        CHECK(other.load(observed.sourceFile).state == OSGBArtifacts::LookupState::Invalid);
    }
    CHECK(cache.load(observed.sourceFile).state == OSGBArtifacts::LookupState::Hit);
}

TEST_CASE("OSGB artifact corruption and cache limits fall back safely", "[osgb][artifacts]")
{
    const auto settings = context("failures");
    auto sample = page(settings);
    OSGBArtifacts cache(settings);
    stored(cache.store(input(settings), *sample.node));
    {
        RestoreFile restore(manifest(settings));
        restore.write("{invalid-json");
        auto broken = cache.load(input(settings).sourceFile);
        CHECK(broken.state == OSGBArtifacts::LookupState::Invalid);
        CHECK_FALSE(broken.node);
    }
    const auto metadata = nlohmann::json::parse(readBoundedFile(manifest(settings), 1024 * 1024));
    {
        RestoreFile restore(settings.cacheRoot / (metadata.at("blobHash").get<std::string>() + ".vsgb"));
        auto changed = restore.original;
        changed.back() ^= 1;
        restore.write(changed);
        CHECK(cache.load(input(settings).sourceFile).state == OSGBArtifacts::LookupState::Invalid);
    }
    auto tiny = context("budget");
    tiny.maxCacheBytes = tiny.maxEntryBytes = 4096;
    fixture(tiny.cacheRoot / "preserved.bin", std::string(4096, 'x'));
    OSGBArtifacts exhausted(tiny);
    auto result = exhausted.store(input(tiny), *sample.node);
    INFO(result.diagnostic);
    CHECK(result.state == OSGBArtifacts::StoreState::BudgetExceeded);
    CHECK(readBoundedFile(tiny.cacheRoot / "preserved.bin", 4096) == std::string(4096, 'x'));

    auto unsafe = settings;
    unsafe.cacheRoot = unsafe.sourceRoot / "cache";
    OSGBArtifacts blocked(unsafe);
    CHECK(blocked.load(input(unsafe).sourceFile).state == OSGBArtifacts::LookupState::Disabled);
    CHECK(blocked.store(input(unsafe), *sample.node).state == OSGBArtifacts::StoreState::Disabled);
    CHECK_FALSE(fs::exists(unsafe.cacheRoot));
}

TEST_CASE("OSGB artifact writer lock and parallel page access are bounded", "[osgb][artifacts]")
{
    auto settings = context("parallel");
    OSGBArtifacts cache(settings);
    auto initial = page(settings);
    stored(cache.store(input(settings), *initial.node));
    std::array<OSGBArtifacts::StoreResult, 4> writes;
    std::array<OSGBArtifacts::Lookup, 4> reads;
    std::array<std::thread, 4> workers;
    for (size_t i = 0; i < workers.size(); ++i)
        workers[i] = std::thread([&, i] {
            auto sample = page(settings);
            writes[i] = cache.store(input(settings), *sample.node);
            reads[i] = cache.load(input(settings).sourceFile);
        });
    for (auto& worker : workers) worker.join();
    for (size_t i = 0; i < workers.size(); ++i)
    {
        if (writes[i].state == OSGBArtifacts::StoreState::Failed)
            CHECK(writes[i].diagnostic.find("writer") != std::string::npos);
        else stored(writes[i]);
        CHECK(reads[i].state == OSGBArtifacts::LookupState::Hit);
        if (i) CHECK(reads[i].node != reads[0].node);
    }
    auto locked = context("writer-lock");
    fixture(locked.cacheRoot / "osgb-pages.writer.lock", "preserved-lock");
    OSGBArtifacts blocked(locked);
    auto sample = page(locked);
    auto result = blocked.store(input(locked), *sample.node);
    CHECK(result.state == OSGBArtifacts::StoreState::Failed);
    CHECK(result.diagnostic.find("writer") != std::string::npos);
    CHECK(readBoundedFile(locked.cacheRoot / "osgb-pages.writer.lock", 64) == "preserved-lock");
}

TEST_CASE("OSGB plain image serialization preserves explicit mip layout and restores ownership", "[osgb][artifacts]")
{
    auto settings = context("plain-images");
    auto sample = page(settings);
    const auto original = imageData(*sample.node);
    { OSGBPlainImages unchanged(*sample.node, 0); CHECK(imageData(*sample.node) == original); }
    auto layout = vsg::MipmapLayout::create(2);
    layout->set(0, vsg::uivec4(2, 2, 1, 0));
    layout->set(1, vsg::uivec4(1, 1, 1, 0));
    auto storage = vsg::ubyteArray::create(20);
    std::memset(storage->dataPointer(), 17, storage->dataSize());
    vsg::Data::Properties properties{VK_FORMAT_R8G8B8A8_UNORM};
    properties.mipLevels = 2;
    auto data = vsg::ubvec4Array2D::create(storage, 0, 4, 2, 2, properties, layout.get());
    sample = page(settings, data);
    OSGBResources resources;
    resources.process(*sample.node);
    auto view = imageData(*sample.node);
    CHECK_THROWS_AS((void)OSGBPlainImages(*sample.node, 0), std::runtime_error);
    CHECK(imageData(*sample.node) == view);
    OSGBArtifacts cache(settings);
    stored(cache.store(input(settings), *sample.node));
    auto hit = cache.load(input(settings).sourceFile);
    REQUIRE(hit.state == OSGBArtifacts::LookupState::Hit);
    const auto restored = imageData(*hit.node);
    REQUIRE(restored->getMipmapLayout());
    CHECK(restored->getMipmapLayout()->size() == 2);
    CHECK(restored->dataSize() == 20);
    CHECK(imageData(*sample.node) == view);
}

TEST_CASE("OSGB native path prefixes preserve source containment checks", "[osgb][artifacts]")
{
    const auto root = source();
    const auto file = root / "Tile" / "root.osgb";
    CHECK(canonicalFilePath(nativeFilePath(file)) == canonicalFilePath(file));
    CHECK(pathWithin(nativeFilePath(file), root));
    CHECK(pathWithin(file, nativeFilePath(root)));
    CHECK_FALSE(pathWithin(base() / "cache", nativeFilePath(root)));
    CHECK_THROWS_AS(validateArtifactDirectory(nativeFilePath(root / "cache"), root), std::runtime_error);
    CHECK_THROWS_AS(validateArtifactDirectory(nativeFilePath(root.parent_path()), root), std::runtime_error);
    CHECK(readBoundedFile(nativeFilePath(file), 1024) == "root-source-v1");
}
