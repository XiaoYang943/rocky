#include "OSGBOverview.h"
#include <osg/Geometry>
#include <osg/Group>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/Texture2D>
#include <osg/TriangleIndexFunctor>
#include <osgUtil/Simplifier>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace geostudio::native::io::detail
{
namespace
{
void checkCancelled(const OverviewSettings& settings)
{
    if (settings.cancelled && settings.cancelled()) throw std::runtime_error("OSGB overview cancelled");
}
struct TriangleCount
{
    uint64_t count = 0;
    void operator()(unsigned a, unsigned b, unsigned c) { if (a != b && b != c && a != c) ++count; }
};
uint64_t triangles(const osg::Geometry& geometry)
{
    osg::TriangleIndexFunctor<TriangleCount> counter;
    geometry.accept(counter);
    return counter.count;
}

// 统计只读数组和图像，不访问外部文件；同一对象只计算一次。
struct Survey : osg::NodeVisitor
{
    explicit Survey(const OverviewSettings& s) : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), settings(s)
    { setNodeMaskOverride(~0u); }
    const OverviewSettings& settings;
    std::unordered_set<const osg::Node*> nodes;
    std::unordered_set<const osg::BufferData*> buffers;
    std::unordered_set<const osg::Image*> images;
    uint64_t triangleCount = 0, vertices = 0, textureBytes = 0, arrayBytes = 0, largestGeometry = 0;
    void state(const osg::StateSet* value)
    {
        if (!value) return;
        for (unsigned unit = 0; unit < value->getTextureAttributeList().size(); ++unit)
        {
            const auto texture = dynamic_cast<const osg::Texture*>(value->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
            if (!texture) continue;
            for (unsigned i = 0; i < texture->getNumImages(); ++i)
                if (const auto image = texture->getImage(i); image && images.insert(image).second)
                {
                    // 固定 OSG 不认识 sRGB BC 像素枚举的存储尺寸，必须在深拷贝前拒绝。
                    if (image->getPixelFormat() >= 0x8C4C && image->getPixelFormat() <= 0x8C4F)
                        throw std::runtime_error("OSGB overview requires fixed-OSG BC storage enums and an explicit color transfer setting");
                    if (image->data() && image->getTotalSizeInBytesIncludingMipmaps() == 0)
                        throw std::runtime_error("OSGB overview image format is not sized by the fixed OSG decoder");
                    textureBytes += image->getTotalSizeInBytesIncludingMipmaps();
                }
        }
    }
    void budget() const
    {
        if (textureBytes > settings.maxResidentBytes || arrayBytes > settings.maxResidentBytes - textureBytes)
            throw std::runtime_error("OSGB overview resident input exceeds budget");
    }
    void apply(osg::Node& node) override
    {
        if (!nodes.insert(&node).second) return;
        checkCancelled(settings);
        if (node.getUpdateCallback() || node.getEventCallback() || node.getCullCallback())
            throw std::runtime_error("OSGB overview requires a static prepared root");
        state(node.getStateSet());
        budget();
        traverse(node);
    }
    void apply(osg::ProxyNode&) override { throw std::runtime_error("OSGB overview does not support ProxyNode references"); }
    void apply(osg::Geometry& geometry) override
    {
        if (!nodes.insert(&geometry).second) return;
        checkCancelled(settings);
        if (geometry.getUpdateCallback() || geometry.getEventCallback() || geometry.getCullCallback())
            throw std::runtime_error("OSGB overview requires static prepared geometry");
        state(geometry.getStateSet());
        if (!geometry.getVertexArray()) throw std::runtime_error("OSGB overview geometry has no vertices");
        vertices += geometry.getVertexArray()->getNumElements();
        const auto count = triangles(geometry);
        if (count > settings.maxTrianglesPerGeometry) throw std::runtime_error("OSGB overview geometry exceeds triangle budget");
        triangleCount += count;
        largestGeometry = std::max(largestGeometry, count);
        auto add = [this](const osg::BufferData* data) {
            if (data && buffers.insert(data).second) arrayBytes += data->getTotalDataSize();
        };
        add(geometry.getVertexArray()); add(geometry.getNormalArray()); add(geometry.getColorArray());
        add(geometry.getSecondaryColorArray()); add(geometry.getFogCoordArray());
        for (unsigned i = 0; i < geometry.getNumTexCoordArrays(); ++i) add(geometry.getTexCoordArray(i));
        for (unsigned i = 0; i < geometry.getNumVertexAttribArrays(); ++i) add(geometry.getVertexAttribArray(i));
        for (unsigned i = 0; i < geometry.getNumPrimitiveSets(); ++i) add(geometry.getPrimitiveSet(i));
        budget();
    }
};

// 副本中的分页节点改为普通 Group，保留现有状态与子节点；不保留任何外部引用。
osg::ref_ptr<osg::Node> stripPages(osg::Node* node, OverviewStats& stats, const OverviewSettings& settings,
    std::unordered_map<osg::Node*, osg::ref_ptr<osg::Node>>& visited)
{
    if (!node) throw std::runtime_error("OSGB overview contains a null node");
    auto found = visited.find(node);
    if (found != visited.end()) return found->second;
    checkCancelled(settings);
    osg::ref_ptr<osg::Node> result = node;
    if (auto page = dynamic_cast<osg::PagedLOD*>(node))
    {
        if (!page->getNumChildren()) throw std::runtime_error("OSGB overview page has no resident fallback");
        result = new osg::Group(*page, osg::CopyOp::SHALLOW_COPY);
        ++stats.pagedLodsRemoved;
    }
    visited.emplace(node, result);
    if (auto group = result->asGroup())
        for (unsigned i = 0; i < group->getNumChildren(); ++i)
            group->setChild(i, stripPages(group->getChild(i), stats, settings, visited));
    return result;
}

struct Edges
{
    unsigned vertexCount = 0;
    std::unordered_map<uint64_t, unsigned> counts;
    void operator()(unsigned a, unsigned b, unsigned c)
    {
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount) throw std::runtime_error("OSGB overview triangle index is invalid");
        if (a == b || b == c || a == c) return;
        for (const auto edge : {std::pair<unsigned, unsigned>{a, b}, {b, c}, {c, a}})
        {
            const auto lo = std::min(edge.first, edge.second), hi = std::max(edge.first, edge.second);
            ++counts[(static_cast<uint64_t>(lo) << 32) | hi];
        }
    }
};
struct Continue : osgUtil::Simplifier::ContinueSimplificationCallback
{
    explicit Continue(const OverviewSettings& s) : settings(s) {}
    const OverviewSettings& settings;
    bool continueSimplification(const osgUtil::Simplifier& simplifier, float error, unsigned original, unsigned remaining) const override
    {
        checkCancelled(settings);
        return simplifier.continueSimplificationImplementation(error, original, remaining);
    }
};

double decodeColor(double value, bool srgb)
{
    return !srgb ? value : value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
double encodeColor(double value, bool srgb)
{
    value = std::clamp(value, 0.0, 1.0);
    return !srgb ? value : value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}
unsigned char byte(double value)
{
    return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

// 仅解码已确认的照片格式；未知压缩不能使用 getColor 的白色兜底冒充成功。
GLenum readableFormat(const osg::Image& image)
{
    const auto format = image.getPixelFormat();
    if (!image.isCompressed())
    {
        if (image.getDataType() == GL_UNSIGNED_BYTE &&
            (format == GL_RGB || format == GL_BGR || format == GL_RGBA || format == GL_BGRA)) return format;
        throw std::runtime_error("OSGB overview requires unsigned-byte RGB/BGR/RGBA/BGRA color images");
    }
    switch (format)
    {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return format;
    default: throw std::runtime_error("OSGB overview compressed texture is not BC1/BC2/BC3");
    }
}
osg::ref_ptr<osg::Image> resizeColor(const osg::Image& source, const OverviewSettings& settings)
{
    if (!source.data() || source.s() <= 0 || source.t() <= 0 || source.r() != 1)
        throw std::runtime_error("OSGB overview requires a valid two-dimensional texture");
    if (static_cast<uint64_t>(source.s()) * static_cast<uint64_t>(source.t()) > settings.maxTexturePixels)
        throw std::runtime_error("OSGB overview texture exceeds pixel budget");
    const auto format = readableFormat(source);
    osg::ref_ptr<osg::Image> view = new osg::Image;
    view->setImage(source.s(), source.t(), 1, source.getInternalTextureFormat(), format, source.getDataType(),
        const_cast<unsigned char*>(source.data()), osg::Image::NO_DELETE, source.getPacking(), source.getRowLength());
    const auto maxSide = std::max(source.s(), source.t());
    const unsigned width = std::max<uint64_t>(1, static_cast<uint64_t>(source.s()) * settings.textureMaxDimension / maxSide);
    const unsigned height = std::max<uint64_t>(1, static_cast<uint64_t>(source.t()) * settings.textureMaxDimension / maxSide);
    osg::ref_ptr<osg::Image> output = new osg::Image;
    output->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, 1);
    output->setOrigin(source.getOrigin());
    for (unsigned y = 0; y < height; ++y)
    {
        checkCancelled(settings);
        const double y0 = static_cast<double>(y) * source.t() / height, y1 = static_cast<double>(y + 1) * source.t() / height;
        for (unsigned x = 0; x < width; ++x)
        {
            const double x0 = static_cast<double>(x) * source.s() / width, x1 = static_cast<double>(x + 1) * source.s() / width;
            double alpha = 0, weight = 0;
            std::array<double, 3> color{};
            for (int sy = static_cast<int>(std::floor(y0)); sy < static_cast<int>(std::ceil(y1)); ++sy)
                for (int sx = static_cast<int>(std::floor(x0)); sx < static_cast<int>(std::ceil(x1)); ++sx)
                {
                    const double area = (std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx))) *
                        (std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy)));
                    const auto pixel = view->getColor(static_cast<unsigned>(sx), static_cast<unsigned>(sy));
                    const double a = std::clamp(static_cast<double>(pixel.a()), 0.0, 1.0);
                    for (unsigned channel = 0; channel < 3; ++channel)
                        color[channel] += decodeColor(pixel[channel], settings.colorTexturesSRGB) * a * area;
                    alpha += a * area;
                    weight += area;
                }
            auto pixel = output->data(x, y);
            for (unsigned channel = 0; channel < 3; ++channel)
                pixel[channel] = byte(encodeColor(alpha > 0 ? color[channel] / alpha : 0, settings.colorTexturesSRGB));
            pixel[3] = byte(weight > 0 ? alpha / weight : 0);
        }
    }
    return output;
}

struct Reduce : osg::NodeVisitor
{
    Reduce(const OverviewSettings& s, OverviewStats& output) : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), settings(s), stats(output)
    { setNodeMaskOverride(~0u); }
    const OverviewSettings& settings;
    OverviewStats& stats;
    std::unordered_set<osg::Node*> nodes;
    std::unordered_set<osg::StateSet*> states;
    std::unordered_map<osg::Texture2D*, osg::ref_ptr<osg::Texture2D>> textures;
    std::unordered_map<osg::Image*, osg::ref_ptr<osg::Image>> images;
    void state(osg::StateSet* value)
    {
        if (!value || !states.insert(value).second) return;
        auto texture = dynamic_cast<osg::Texture2D*>(value->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
        if (!texture || !texture->getImage()) return;
        auto source = texture->getImage();
        if (source->s() <= static_cast<int>(settings.textureMaxDimension) && source->t() <= static_cast<int>(settings.textureMaxDimension)) return;
        auto found = textures.find(texture);
        osg::ref_ptr<osg::Texture2D> replacement;
        if (found != textures.end()) replacement = found->second;
        else
        {
            auto imageFound = images.find(source);
            osg::ref_ptr<osg::Image> image;
            if (imageFound != images.end()) image = imageFound->second;
            else
            {
                image = resizeColor(*source, settings);
                images.emplace(source, image);
                ++stats.texturesResized;
            }
            replacement = new osg::Texture2D(*texture, osg::CopyOp::SHALLOW_COPY);
            replacement->setImage(image);
            replacement->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            textures.emplace(texture, replacement);
        }
        const auto pair = value->getTextureAttributePair(0, osg::StateAttribute::TEXTURE);
        const auto flags = pair ? pair->second : osg::StateAttribute::OFF;
        value->setTextureAttribute(0, replacement, flags);
    }
    void apply(osg::Node& node) override
    {
        if (!nodes.insert(&node).second) return;
        checkCancelled(settings);
        state(node.getStateSet());
        traverse(node);
    }
    void apply(osg::Geometry& geometry) override
    {
        if (!nodes.insert(&geometry).second) return;
        checkCancelled(settings);
        state(geometry.getStateSet());
        if (settings.triangleRatio >= 1.0 || triangles(geometry) < 2) return;
        osg::TriangleIndexFunctor<Edges> edges;
        edges.vertexCount = geometry.getVertexArray()->getNumElements();
        geometry.accept(edges);
        std::unordered_set<unsigned> boundary;
        for (const auto& edge : edges.counts)
            if (edge.second != 2)
            {
                boundary.insert(static_cast<unsigned>(edge.first >> 32));
                boundary.insert(static_cast<unsigned>(edge.first));
            }
        osgUtil::Simplifier::IndexList protectedPoints(boundary.begin(), boundary.end());
        osgUtil::Simplifier simplifier(settings.triangleRatio, settings.maximumError);
        simplifier.setSmoothing(false);
        simplifier.setDoTriStrip(false);
        simplifier.setContinueSimplificationCallback(new Continue(settings));
        simplifier.simplify(geometry, protectedPoints);
        checkCancelled(settings);
        geometry.dirtyBound();
    }
};
}

OverviewResult reduceResident(const osg::Node& preparedRoot, const OverviewSettings& settings)
{
    if (!std::isfinite(settings.triangleRatio) || settings.triangleRatio <= 0 || settings.triangleRatio > 1 ||
        !std::isfinite(settings.maximumError) || settings.maximumError < 0 || settings.maximumError > std::numeric_limits<float>::max() ||
        !settings.textureMaxDimension || settings.textureMaxDimension > 16384 || !settings.maxResidentBytes ||
        !settings.maxTrianglesPerGeometry || !settings.maxTexturePixels)
        throw std::runtime_error("Invalid OSGB overview reduction settings");
    checkCancelled(settings);
    Survey before(settings);
    const_cast<osg::Node&>(preparedRoot).accept(before);
    if (!before.triangleCount) throw std::runtime_error("OSGB overview has no resident triangles");
    OverviewResult result;
    result.stats.inputTriangles = before.triangleCount;
    result.stats.inputVertices = before.vertices;
    result.stats.inputTextureBytes = before.textureBytes;
    // 包含输入、副本、边表及简化器工作集的保守估算；第三方分配器开销不构成硬上限。
    result.stats.estimatedPeakBytes = (before.arrayBytes + before.textureBytes) * 2 + before.largestGeometry * 512;
    osg::ref_ptr<osg::Node> copy = dynamic_cast<osg::Node*>(preparedRoot.clone(osg::CopyOp::DEEP_COPY_ALL));
    if (!copy) throw std::runtime_error("Cannot clone OSGB overview root");
    std::unordered_map<osg::Node*, osg::ref_ptr<osg::Node>> stripped;
    result.node = stripPages(copy, result.stats, settings, stripped);
    Reduce reduce(settings, result.stats);
    result.node->accept(reduce);
    Survey after(settings);
    result.node->accept(after);
    if (!after.triangleCount) throw std::runtime_error("OSGB overview simplification removed all triangles");
    result.stats.outputTriangles = after.triangleCount;
    result.stats.outputVertices = after.vertices;
    result.stats.outputTextureBytes = after.textureBytes;
    checkCancelled(settings);
    return result;
}
}
