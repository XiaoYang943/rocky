#include "OSGBSerialization.h"
#include <vsg/all.h>
#include <cstring>
#include <set>
#include <stdexcept>

namespace geostudio::native::io::detail
{
namespace
{
template<class T> bool standalone(const vsg::Data& source)
{
    const auto* array = dynamic_cast<const vsg::Array2D<T>*>(&source);
    return array && !array->storage();
}
bool standalone(const vsg::Data& source)
{
    return standalone<uint8_t>(source) || standalone<uint16_t>(source) || standalone<uint32_t>(source) ||
        standalone<float>(source) || standalone<double>(source) || standalone<vsg::ubvec2>(source) ||
        standalone<vsg::ubvec3>(source) || standalone<vsg::ubvec4>(source) || standalone<vsg::usvec2>(source) ||
        standalone<vsg::usvec3>(source) || standalone<vsg::usvec4>(source) || standalone<vsg::uivec2>(source) ||
        standalone<vsg::uivec3>(source) || standalone<vsg::uivec4>(source) || standalone<vsg::vec2>(source) ||
        standalone<vsg::vec3>(source) || standalone<vsg::vec4>(source) || standalone<vsg::dvec2>(source) ||
        standalone<vsg::dvec3>(source) || standalone<vsg::dvec4>(source) ||
        standalone<vsg::block64>(source) || standalone<vsg::block128>(source);
}
template<class T> vsg::ref_ptr<vsg::Data> plainImage(const vsg::Data& source)
{
    auto properties = source.properties;
    properties.allocatorType = vsg::ALLOCATOR_TYPE_NEW_DELETE;
    auto bytes = std::make_unique<T[]>(source.valueCount());
    std::memcpy(bytes.get(), source.dataPointer(), source.dataSize());
    auto result = vsg::Array2D<T>::create(source.width(), source.height(), bytes.get(), properties,
        const_cast<vsg::MipmapLayout*>(source.getMipmapLayout()));
    bytes.release();
    if (source.getAuxiliary()) result->getOrCreateAuxiliary()->userObjects = source.getAuxiliary()->userObjects;
    return result;
}
vsg::ref_ptr<vsg::Data> plainImage(const vsg::Data& source)
{
    // storage 视图在固定 VSG 的 read(assign) 中会清除 MipmapLayout；独立数组保留全部 Mip。
    if (source.dimensions() != 2 || !source.contiguous() || !source.dataAvailable() ||
        source.dataSize() != source.valueSize() * source.valueCount())
        throw std::runtime_error("OSGB serialization requires contiguous resident 2D image data");
    switch (source.valueSize())
    {
    case 1: return plainImage<uint8_t>(source);
    case 2: return plainImage<uint16_t>(source);
    case 3: return plainImage<vsg::ubvec3>(source);
    case 4: return plainImage<vsg::ubvec4>(source);
    case 6: return plainImage<vsg::usvec3>(source);
    case 8: return plainImage<vsg::block64>(source);
    case 12: return plainImage<vsg::vec3>(source);
    case 16: return plainImage<vsg::block128>(source);
    case 24: return plainImage<vsg::dvec3>(source);
    case 32: return plainImage<vsg::dvec4>(source);
    default: throw std::runtime_error("OSGB serialization image value size is unsupported");
    }
}
}
struct OSGBPlainImages::Impl : vsg::Visitor
{
    uint64_t remaining;
    std::set<vsg::Object*> visited;
    std::set<vsg::Image*> images;
    std::vector<std::pair<vsg::ref_ptr<vsg::Image>, vsg::ref_ptr<vsg::Data>>> originals;
    explicit Impl(uint64_t maximum) : remaining(maximum) {}
    ~Impl() override { for (auto& entry : originals) entry.first->data = entry.second; }
    void apply(vsg::Object& object) override
    {
        if (visited.insert(&object).second) object.traverse(*this);
    }
    void apply(vsg::PagedLOD& page) override
    {
        if (!visited.insert(&page).second) return;
        for (const auto& child : page.children) if (child.node) child.node->accept(*this);
    }
    void apply(vsg::DescriptorImage& descriptor) override
    {
        if (!visited.insert(&descriptor).second) return;
        for (const auto& info : descriptor.imageInfoList)
        {
            if (!info || !info->imageView || !info->imageView->image || !info->imageView->image->data)
                throw std::runtime_error("OSGB serialization image lacks CPU data");
            auto image = info->imageView->image;
            if (!images.insert(image.get()).second) continue;
            if (standalone(*image->data)) continue;
            if (image->data->dataSize() > remaining) throw std::runtime_error("OSGB serialized images exceed byte budget");
            remaining -= image->data->dataSize();
            auto plain = plainImage(*image->data);
            originals.emplace_back(image, image->data);
            image->data = plain;
        }
    }
};
OSGBPlainImages::OSGBPlainImages(vsg::Node& node, uint64_t maximumBytes) : _impl(std::make_unique<Impl>(maximumBytes))
{
    node.accept(*_impl);
}
OSGBPlainImages::~OSGBPlainImages() = default;
}
