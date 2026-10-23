#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBDecode.h"
#include "../../src/render/import/osgb/OSGBFiles.h"
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/Texture2D>
#include <osgDB/Options>
#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <array>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

using geostudio::native::io::detail::OSGBDecode;
namespace fs = std::filesystem;

namespace
{
// fixture 归属已有 build/osgb-fixtures 下的 decode 子目录；按内容复用，不覆盖不一致文件。
void saveDecodeFixture(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    if (fs::exists(path))
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(std::string(std::istreambuf_iterator<char>(input), {}) == bytes);
    }
    else
    {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
    }
}

osg::ref_ptr<osg::Geode> decodeTriangle(osg::Image* image)
{
    auto geometry = new osg::Geometry;
    auto vertices = new osg::Vec3Array;
    vertices->push_back({0, 0, 0}); vertices->push_back({1, 0, 0}); vertices->push_back({0, 1, 0});
    geometry->setVertexArray(vertices);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(image));
    auto result = new osg::Geode;
    result->addDrawable(geometry);
    return result;
}

fs::path decodeFixtures()
{
    static const auto root = [] {
        auto directory = fs::u8path(GN_OSGB_FIXTURES) / "decode";
        auto image = new osg::Image;
        osg::ref_ptr<osg::Image> imageOwner = image;
        image->allocateImage(2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        std::fill(image->data(), image->data() + 16, 128);
        image->setFileName("tex.png");
        auto png = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        REQUIRE(png);
        std::ostringstream imageBytes(std::ios::binary);
        REQUIRE(png->writeImage(*image, imageBytes).success());
        saveDecodeFixture(directory / "tex.png", imageBytes.str());
        auto writer = osgDB::Registry::instance()->getReaderWriterForExtension("osgb");
        REQUIRE(writer);
        auto write = [&](const std::string& filename, const osg::Node& node, const std::string& hint) {
            osg::ref_ptr<osgDB::Options> options = new osgDB::Options("WriteImageHint=" + hint);
            options->setPluginStringData("fileType", "Binary");
            std::ostringstream output(std::ios::binary);
            REQUIRE(writer->writeNode(node, output, options).success());
            saveDecodeFixture(directory / filename, output.str());
        };
        auto geometry = decodeTriangle(image);
        write("external.osgb", *geometry, "UseExternal");
        write("embedded.osgb", *geometry, "IncludeData");
        image->setFileName("paused.decodehold");
        write("paused.osgb", *geometry, "UseExternal");
        saveDecodeFixture(directory / "paused.decodehold", "x");
        image->setFileName("tex.png");
        osg::ref_ptr<osg::PagedLOD> paged = new osg::PagedLOD;
        paged->addChild(geometry, 0, 100);
        paged->setFileName(1, "unread-fine.osgb");
        paged->setRange(1, 100, FLT_MAX);
        write("paged.osgb", *paged, "IncludeData");
        osg::ref_ptr<osg::ProxyNode> proxy = new osg::ProxyNode;
        proxy->setLoadingExternalReferenceMode(osg::ProxyNode::LOAD_IMMEDIATELY);
        proxy->setFileName(0, "embedded.osgb");
        write("proxy.osgb", *proxy, "IncludeData");
        saveDecodeFixture(directory / "broken.osgb", "invalid OSGB bytes");
        return directory;
    }();
    return root;
}

osg::Image* decodedImage(osg::Node* node)
{
    auto geode = dynamic_cast<osg::Geode*>(node);
    if (!geode || geode->getNumDrawables() != 1) return nullptr;
    auto state = geode->getDrawable(0)->getStateSet();
    if (!state) return nullptr;
    auto texture = dynamic_cast<osg::Texture2D*>(state->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
    return texture ? texture->getImage() : nullptr;
}

// 专属测试扩展在真实外部图片回调中阻塞，确定性地制造一个在途解码和一个等待准入的请求。
struct HoldingImageReader : osgDB::ReaderWriter
{
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    mutable bool entered = false, released = false;
    HoldingImageReader() { supportsExtension("decodehold", "Bounded decode cancellation fixture"); }
    ReadResult readImage(const std::string& file, const osgDB::Options*) const override
    {
        if (!acceptsExtension(osgDB::getLowerCaseFileExtension(file))) return ReadResult::FILE_NOT_HANDLED;
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; }))
            return ReadResult("Decode fixture release timed out");
        auto image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        std::fill(image->data(), image->data() + 4, 255);
        return image;
    }
};
}

TEST_CASE("OSGB decode tracks actual external images and keeps embedded names out of dependencies", "[osgb][decode]")
{
    const auto root = decodeFixtures();
    OSGBDecode decoder;
    auto external = decoder.read(root / "external.osgb");
    REQUIRE(external.node);
    REQUIRE(external.externalImages.size() == 1);
    CHECK(external.externalImages.front() == fs::weakly_canonical(root / "tex.png"));
    CHECK(external.sourceSha256 == geostudio::native::io::detail::sha256File(root / "external.osgb"));
    CHECK(external.externalImageHashes.at(external.externalImages.front()) == geostudio::native::io::detail::sha256File(root / "tex.png"));
    REQUIRE(decodedImage(external.node));
    CHECK(decodedImage(external.node)->s() == 2);
    auto again = decoder.read(root / "external.osgb");
    CHECK(again.node != external.node);
    CHECK(decodedImage(again.node) != decodedImage(external.node));
    auto embedded = decoder.read(root / "embedded.osgb");
    REQUIRE(embedded.node);
    CHECK(embedded.externalImages.empty());
    CHECK(embedded.externalImageHashes.empty());
    CHECK(decoder.statistics().completed == 3);
    CHECK(decoder.statistics().inFlight == 0);
    CHECK(decoder.statistics().ioMilliseconds > 0.0);
    CHECK(decoder.statistics().decodeMilliseconds > 0.0);
}

TEST_CASE("OSGB decode keeps PagedLOD lazy and rejects eager external nodes", "[osgb][decode]")
{
    const auto root = decodeFixtures();
    OSGBDecode decoder;
    auto decoded = decoder.read(root / "paged.osgb");
    auto paged = dynamic_cast<osg::PagedLOD*>(decoded.node.get());
    REQUIRE(paged);
    CHECK(paged->getNumChildren() == 1);
    CHECK(paged->getFileName(1) == "unread-fine.osgb");
    CHECK_THROWS_WITH(decoder.read(root / "proxy.osgb"), "OSGB ProxyNode/external nodes must be loaded through the VSG pager");
    CHECK(decoder.statistics().failed == 1);
    CHECK(decoder.statistics().inFlight == 0);
}

TEST_CASE("OSGB decode enforces bounded concurrent input without sharing decoded nodes", "[osgb][decode]")
{
    const auto root = decodeFixtures();
    OSGBDecode::Settings settings;
    settings.maxInFlight = 2;
    OSGBDecode decoder(settings);
    std::array<osg::ref_ptr<osg::Node>, 8> nodes;
    std::array<std::exception_ptr, 8> errors{};
    std::array<std::thread, 8> threads;
    std::atomic_bool start{false};
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i] = std::thread([&, i] {
            while (!start.load()) std::this_thread::yield();
            try { nodes[i] = decoder.read(root / (i % 2 ? "external.osgb" : "embedded.osgb")).node; }
            catch (...) { errors[i] = std::current_exception(); }
        });
    start = true;
    for (auto& thread : threads) thread.join();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        REQUIRE_FALSE(errors[i]);
        REQUIRE(nodes[i]);
        for (size_t j = 0; j < i; ++j) CHECK(nodes[i] != nodes[j]);
    }
    const auto stats = decoder.statistics();
    CHECK(stats.requests == nodes.size());
    CHECK(stats.completed == nodes.size());
    CHECK(stats.failed == 0);
    CHECK(stats.inFlight == 0);
    CHECK(stats.peakInFlight >= 1);
    CHECK(stats.peakInFlight <= 2);
}

TEST_CASE("OSGB decode close rejects late work and size failures release admission", "[osgb][decode]")
{
    const auto root = decodeFixtures();
    OSGBDecode::Settings tiny;
    tiny.maxInFlight = 1;
    tiny.maxSourceBytes = 8;
    OSGBDecode limited(tiny);
    CHECK_THROWS(limited.read(root / "embedded.osgb"));
    CHECK(limited.statistics().failed == 1);
    CHECK(limited.statistics().inFlight == 0);

    OSGBDecode decoder;
    CHECK_THROWS(decoder.read(root / "broken.osgb"));
    CHECK(decoder.statistics().inFlight == 0);
    decoder.close(); decoder.close();
    std::array<std::thread, 8> threads;
    std::atomic_uint cancelled{0};
    for (auto& thread : threads)
        thread = std::thread([&] {
            try { decoder.read(root / "embedded.osgb"); }
            catch (const std::runtime_error& error) { if (std::string(error.what()) == "OSGB decode cancelled") ++cancelled; }
        });
    for (auto& thread : threads) thread.join();
    CHECK(cancelled == threads.size());
    CHECK(decoder.statistics().cancelled == threads.size());
    CHECK(decoder.statistics().inFlight == 0);
    OSGBDecode fresh;
    CHECK(fresh.read(root / "embedded.osgb").node.valid());
    OSGBDecode::Settings invalid;
    invalid.maxInFlight = 5;
    CHECK_THROWS_AS((void)OSGBDecode{invalid}, std::invalid_argument);
}

TEST_CASE("OSGB decode close wakes admission waiters while an active codec finishes", "[osgb][decode]")
{
    const auto root = decodeFixtures();
    osg::ref_ptr<HoldingImageReader> holding = new HoldingImageReader;
    osgDB::Registry::instance()->addReaderWriter(holding);
    OSGBDecode::Settings settings;
    settings.maxInFlight = 1;
    OSGBDecode decoder(settings);
    std::array<std::string, 2> outcomes;
    std::thread active([&] {
        try { decoder.read(root / "paused.osgb"); }
        catch (const std::exception& error) { outcomes[0] = error.what(); }
    });
    bool entered;
    {
        std::unique_lock<std::mutex> lock(holding->mutex);
        entered = holding->changed.wait_for(lock, std::chrono::seconds(2), [&] { return holding->entered; });
    }
    std::thread queued([&] {
        try { decoder.read(root / "embedded.osgb"); }
        catch (const std::exception& error) { outcomes[1] = error.what(); }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (decoder.statistics().requests != 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    decoder.close();
    queued.join();
    {
        std::lock_guard<std::mutex> lock(holding->mutex);
        holding->released = true;
    }
    holding->changed.notify_all();
    active.join();
    osgDB::Registry::instance()->removeReaderWriter(holding);
    REQUIRE(entered);
    CHECK(outcomes[0] == "OSGB decode cancelled");
    CHECK(outcomes[1] == "OSGB decode cancelled");
    CHECK(decoder.statistics().cancelled == 2);
    CHECK(decoder.statistics().inFlight == 0);
    CHECK(decoder.statistics().peakInFlight == 1);
}
