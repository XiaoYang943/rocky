#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBOverview.h"
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/Texture2D>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

using namespace geostudio::native::io::detail;
namespace
{
osg::ref_ptr<osg::Geometry> grid(unsigned cells)
{
    osg::ref_ptr<osg::Geometry> result = new osg::Geometry;
    auto vertices = new osg::Vec3Array;
    auto uv = new osg::Vec2Array;
    for (unsigned y = 0; y <= cells; ++y)
        for (unsigned x = 0; x <= cells; ++x)
        {
            vertices->push_back(osg::Vec3(x, y, 0));
            uv->push_back(osg::Vec2(static_cast<float>(x) / cells, static_cast<float>(y) / cells));
        }
    auto indices = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (unsigned y = 0; y < cells; ++y)
        for (unsigned x = 0; x < cells; ++x)
        {
            const auto a = y * (cells + 1) + x, b = a + 1, c = a + cells + 1, d = c + 1;
            indices->push_back(a); indices->push_back(b); indices->push_back(c);
            indices->push_back(b); indices->push_back(d); indices->push_back(c);
        }
    result->setVertexArray(vertices);
    result->setTexCoordArray(0, uv);
    result->addPrimitiveSet(indices);
    result->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    return result;
}
osg::ref_ptr<osg::Geode> leaf(osg::Geometry* geometry)
{
    osg::ref_ptr<osg::Geode> result = new osg::Geode;
    result->addDrawable(geometry);
    return result;
}
struct Find : osg::NodeVisitor
{
    Find() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
    osg::Geometry* geometry = nullptr;
    unsigned pages = 0;
    void apply(osg::Geometry& value) override { geometry = &value; }
    void apply(osg::PagedLOD& value) override { ++pages; traverse(value); }
};
osg::Geometry* find(osg::Node& node)
{
    Find visitor;
    node.accept(visitor);
    REQUIRE(visitor.geometry);
    return visitor.geometry;
}
osg::Image* image(osg::Geometry& geometry, unsigned unit = 0)
{
    auto texture = dynamic_cast<osg::Texture2D*>(geometry.getStateSet()->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
    REQUIRE(texture);
    REQUIRE(texture->getImage());
    return texture->getImage();
}
osg::ref_ptr<osg::Image> rgba(unsigned width, unsigned height)
{
    osg::ref_ptr<osg::Image> result = new osg::Image;
    result->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, 1);
    std::fill(result->data(), result->data() + width * height * 4, 255);
    return result;
}
}

TEST_CASE("OSGB overview owns reduced resident geometry and preserves tile boundaries", "[osgb][osgb-overview]")
{
    const auto geometry = grid(8);
    const auto original = dynamic_cast<osg::Vec3Array*>(geometry->getVertexArray());
    const std::vector<osg::Vec3> originalVertices(original->begin(), original->end());
    osg::ref_ptr<osg::PagedLOD> source = new osg::PagedLOD;
    source->addChild(leaf(geometry), 0, 100);
    source->setFileName(1, "never-read-external.osgb");
    source->setRange(1, 100, 1e30f);
    source->setName("resident-region");
    OverviewSettings settings;
    settings.triangleRatio = 0.35;
    const auto result = reduceResident(*source, settings);
    REQUIRE(result.node);
    CHECK(result.stats.inputTriangles == 128);
    CHECK(result.stats.outputTriangles < result.stats.inputTriangles);
    CHECK(result.stats.outputVertices < result.stats.inputVertices);
    CHECK(result.stats.pagedLodsRemoved == 1);
    CHECK(result.node->getName() == "resident-region");
    Find visitor;
    result.node->accept(visitor);
    CHECK(visitor.pages == 0);
    REQUIRE(visitor.geometry);
    auto reduced = dynamic_cast<osg::Vec3Array*>(visitor.geometry->getVertexArray());
    REQUIRE(reduced);
    for (const auto& vertex : originalVertices)
        if (vertex.x() == 0 || vertex.y() == 0 || vertex.x() == 8 || vertex.y() == 8)
            CHECK(std::find(reduced->begin(), reduced->end(), vertex) != reduced->end());
    CHECK(std::equal(original->begin(), original->end(), originalVertices.begin(), originalVertices.end()));
    CHECK(source->getFileName(1) == "never-read-external.osgb");
    CHECK(geometry->getNumPrimitiveSets() == 1);
}

TEST_CASE("OSGB overview downsamples color in the selected transfer function", "[osgb][osgb-overview]")
{
    const auto geometry = grid(1);
    auto input = rgba(2, 2);
    input->setOrigin(osg::Image::TOP_LEFT);
    for (unsigned y = 0; y < 2; ++y)
    {
        auto black = input->data(0, y);
        black[0] = black[1] = black[2] = 0;
    }
    auto texture = new osg::Texture2D(input);
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture);
    // 单位零和数值单位刻意共享对象，颜色下采样不能污染数值语义。
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(1, texture);
    const auto node = leaf(geometry);
    OverviewSettings settings;
    settings.triangleRatio = 1;
    settings.textureMaxDimension = 1;
    auto srgb = reduceResident(*node, settings);
    auto reduced = image(*find(*srgb.node));
    CHECK(reduced->s() == 1);
    CHECK(reduced->t() == 1);
    CHECK(reduced->getOrigin() == osg::Image::TOP_LEFT);
    CHECK(reduced->getNumMipmapLevels() == 1);
    CHECK(reduced->data()[0] == 188);
    CHECK(reduced->data()[3] == 255);
    CHECK(image(*find(*srgb.node), 1)->s() == 2);
    CHECK(image(*geometry)->s() == 2);
    settings.colorTexturesSRGB = false;
    const auto linear = reduceResident(*node, settings);
    CHECK(image(*find(*linear.node))->data()[0] == 128);
    CHECK(srgb.stats.texturesResized == 1);
}

TEST_CASE("OSGB overview filters premultiplied alpha and returns straight RGBA", "[osgb][osgb-overview]")
{
    const auto geometry = grid(1);
    osg::ref_ptr<osg::Image> input = new osg::Image;
    input->setImage(2, 2, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE,
        new unsigned char[20]{}, osg::Image::USE_NEW_DELETE, 1);
    for (unsigned y = 0; y < 2; ++y)
    {
        auto red = input->data(0, y);
        red[0] = 255; red[1] = 0; red[2] = 0; red[3] = 255;
        auto transparent = input->data(1, y);
        transparent[0] = 0; transparent[1] = 0; transparent[2] = 255; transparent[3] = 0;
    }
    // 源含完整分配的 1×1 mip，偏移不能跟随较小的新图像。
    input->setMipmapLevels(osg::Image::MipmapDataType{16});
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(input));
    OverviewSettings settings;
    settings.triangleRatio = 1;
    settings.textureMaxDimension = 1;
    const auto result = reduceResident(*leaf(geometry), settings);
    auto output = image(*find(*result.node));
    CHECK(output->data()[0] == 255);
    CHECK(output->data()[1] == 0);
    CHECK(output->data()[2] == 0);
    CHECK(output->data()[3] == 128);
    CHECK(output->getNumMipmapLevels() == 1);
    CHECK(input->getNumMipmapLevels() == 2);
}

TEST_CASE("OSGB overview reads BC blocks on CPU and rejects unsupported reduction", "[osgb][osgb-overview]")
{
    const auto geometry = grid(1);
    OverviewSettings settings;
    settings.triangleRatio = 1;
    settings.textureMaxDimension = 1;
    for (const GLenum format : {GLenum(GL_COMPRESSED_RGB_S3TC_DXT1_EXT), GLenum(GL_COMPRESSED_RGBA_S3TC_DXT3_EXT),
        GLenum(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)})
    {
        const bool alphaBlock = format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        auto bytes = new unsigned char[alphaBlock ? 16 : 8]{};
        if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) std::fill(bytes, bytes + 8, 255);
        if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) bytes[0] = 255;
        const unsigned colorOffset = alphaBlock ? 8 : 0;
        bytes[colorOffset] = 0x00; bytes[colorOffset + 1] = 0xf8; // RGB565 红色，颜色索引全为零。
        osg::ref_ptr<osg::Image> input = new osg::Image;
        input->setImage(4, 4, 1, format, format, GL_UNSIGNED_BYTE, bytes, osg::Image::USE_NEW_DELETE, 1);
        geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(input));
        const auto result = reduceResident(*leaf(geometry), settings);
        auto output = image(*find(*result.node));
        CHECK(output->getPixelFormat() == GL_RGBA);
        CHECK(output->data()[0] == 255);
        CHECK(output->data()[1] == 0);
        CHECK(output->data()[2] == 0);
        CHECK(output->data()[3] == 255);
    }
    settings.maxTexturePixels = 8;
    CHECK_THROWS(reduceResident(*leaf(geometry), settings));
    auto unsupported = new osg::Image;
    unsupported->allocateImage(2, 2, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, 1);
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, new osg::Texture2D(unsupported));
    settings.maxTexturePixels = 16;
    CHECK_THROWS(reduceResident(*leaf(geometry), settings));
}

TEST_CASE("OSGB overview enforces scale budgets and cancellation without changing input", "[osgb][osgb-overview]")
{
    const auto geometry = grid(4);
    const auto source = leaf(geometry);
    const auto count = geometry->getVertexArray()->getNumElements();
    OverviewSettings settings;
    settings.maxResidentBytes = 1;
    CHECK_THROWS(reduceResident(*source, settings));
    settings = OverviewSettings{};
    settings.maxTrianglesPerGeometry = 2;
    CHECK_THROWS(reduceResident(*source, settings));
    settings = OverviewSettings{};
    settings.cancelled = [] { return true; };
    CHECK_THROWS(reduceResident(*source, settings));
    settings = OverviewSettings{};
    unsigned checks = 0;
    settings.cancelled = [&checks] { return ++checks > 6; };
    CHECK_THROWS(reduceResident(*source, settings));
    CHECK(geometry->getVertexArray()->getNumElements() == count);
    settings = OverviewSettings{};
    settings.triangleRatio = 0;
    CHECK_THROWS(reduceResident(*source, settings));
    settings = OverviewSettings{};
    osg::ref_ptr<osg::PagedLOD> emptyPage = new osg::PagedLOD;
    emptyPage->setFileName(0, "never-read-empty.osgb");
    CHECK_THROWS(reduceResident(*emptyPage, settings));
    osg::ref_ptr<osg::ProxyNode> proxy = new osg::ProxyNode;
    proxy->addChild(source);
    proxy->setFileName(0, "never-read-proxy.osgb");
    CHECK_THROWS(reduceResident(*proxy, settings));
}
