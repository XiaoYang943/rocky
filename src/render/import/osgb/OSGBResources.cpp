#include "OSGBResources.h"
#include <vsg/all.h>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace geostudio::native::io::detail
{
namespace
{
using Clock = std::chrono::steady_clock;

// 键只序列化新页的 CPU 值；二进制保留精确浮点位，不比较已编译对象或 Vulkan 句柄。
std::string valueKey(const vsg::Object* object)
{
    std::ostringstream stream(std::ios::binary | std::ios::out);
    auto options = vsg::Options::create();
    options->extensionHint = ".vsgb";
    vsg::VSG writer;
    if (!writer.write(object, stream, options)) throw std::runtime_error("OSGB resource key serialization failed");
    return stream.str();
}

// 长度前缀避免多段二进制键拼接产生歧义。
void appendKey(std::string& target, const std::string& part)
{
    target += std::to_string(part.size());
    target += ':';
    target += part;
}

// 有源码时忽略后续生成的 SPIR-V；源码、编译选项及阶段仍完整区分变体。
std::string stageKey(const vsg::ShaderStage& stage)
{
    if (!stage.module) throw std::runtime_error("OSGB shader stage has no module");
    auto snapshot = vsg::ShaderStage::create(stage);
    snapshot->module = vsg::ShaderModule::create(*stage.module);
    if (!snapshot->module->source.empty()) snapshot->module->code.clear();
    return std::to_string(stage.flags) + ':' + valueKey(snapshot);
}

// 只切换颜色编码语义，不重写像素或 Alpha；未列出的浮点/数值格式保持原样。
VkFormat colorFormat(VkFormat format, OSGBColorSpace policy)
{
    const std::pair<VkFormat, VkFormat> pairs[] = {
        {VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8_SRGB},
        {VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_B8G8R8_SRGB},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB},
        {VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC1_RGB_SRGB_BLOCK},
        {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
        {VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK},
        {VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK}};
    for (const auto& pair : pairs)
        if (format == pair.first || format == pair.second)
            return policy == OSGBColorSpace::SRGB ? pair.second : pair.first;
    return format;
}

template<class T>
vsg::ref_ptr<vsg::Data> colorData(vsg::ref_ptr<vsg::Data> source, VkFormat format)
{
    auto properties = source->properties;
    properties.format = format;
    // 语义视图独立持有原始字节，无像素复制；同时保留压缩块及全部 Mip 的布局和寿命。
    return vsg::Array2D<T>::create(source, 0, properties.stride, source->width(), source->height(), properties,
        const_cast<vsg::MipmapLayout*>(source->getMipmapLayout()));
}

vsg::ref_ptr<vsg::ImageView> colorView(const vsg::ImageView& source, VkFormat format, OSGBColorSpace policy)
{
    if (!source.image || !source.image->data || source.image->imageType != VK_IMAGE_TYPE_2D)
        throw std::runtime_error("OSGB color texture requires uncompiled 2D image data");
    const auto& old = *source.image;
    const auto dataFormat = colorFormat(old.data->properties.format, policy);
    vsg::ref_ptr<vsg::Data> data;
    switch (old.data->valueSize())
    {
    case 3: data = colorData<vsg::ubvec3>(old.data, dataFormat); break;
    case 4: data = colorData<vsg::ubvec4>(old.data, dataFormat); break;
    case 8: data = colorData<vsg::block64>(old.data, dataFormat); break;
    case 16: data = colorData<vsg::block128>(old.data, dataFormat); break;
    default: throw std::runtime_error("OSGB color texture has unsupported element size");
    }
    auto image = vsg::Image::create(data);
    image->flags = old.flags; image->imageType = old.imageType; image->format = format;
    image->extent = old.extent; image->mipLevels = old.mipLevels; image->arrayLayers = old.arrayLayers;
    image->samples = old.samples; image->tiling = old.tiling; image->usage = old.usage;
    image->sharingMode = old.sharingMode; image->queueFamilyIndices = old.queueFamilyIndices;
    image->initialLayout = old.initialLayout;
    auto view = vsg::ImageView::create(image);
    view->flags = source.flags; view->viewType = source.viewType; view->format = format;
    view->components = source.components; view->subresourceRange = source.subresourceRange;
    return view;
}

// GLSLang 进程初始化和冷编译跨 Reader 串行；命中缓存不进入此锁。
std::mutex shaderCompilerMutex;

struct Elapsed
{
    double& total;
    Clock::time_point start = Clock::now();
    ~Elapsed() { total += std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
};
}

struct OSGBResources::Impl
{
    mutable std::mutex mutex;
    Statistics totals;
    std::map<std::string, vsg::ref_ptr<vsg::GraphicsPipeline>> pipelines;
    std::map<std::string, vsg::ref_ptr<vsg::ShaderModule>> modules;
    std::map<std::string, vsg::ref_ptr<vsg::PipelineLayout>> layouts;
    std::map<std::string, vsg::ref_ptr<vsg::DescriptorSetLayout>> setLayouts;
    std::map<std::string, vsg::ref_ptr<vsg::Sampler>> samplers;

    // 每类缓存最多保留有限个变体；移除缓存引用不影响已挂接图持有的资源。
    template<class T>
    vsg::ref_ptr<T> intern(std::map<std::string, vsg::ref_ptr<T>>& cache, const std::string& key,
        vsg::ref_ptr<T> value, uint64_t& created, uint64_t& hits)
    {
        auto found = cache.find(key);
        if (found != cache.end()) { ++hits; return found->second; }
        if (cache.size() >= 256) cache.erase(cache.begin());
        cache.emplace(key, value);
        ++created;
        return value;
    }

    vsg::ref_ptr<vsg::DescriptorSetLayout> layout(vsg::ref_ptr<vsg::DescriptorSetLayout> source)
    {
        if (!source) return {};
        for (const auto& binding : source->bindings)
            if (binding.pImmutableSamplers) throw std::runtime_error("OSGB immutable sampler layout is unsupported");
        return intern(setLayouts, valueKey(source), source, totals.layoutsCreated, totals.layoutHits);
    }

    vsg::ref_ptr<vsg::PipelineLayout> layout(vsg::ref_ptr<vsg::PipelineLayout> source)
    {
        if (!source) return {};
        const auto key = valueKey(source);
        auto existing = layouts.find(key);
        if (existing != layouts.end()) { ++totals.layoutHits; return existing->second; }
        for (auto& item : source->setLayouts) item = layout(item);
        return intern(layouts, key, source, totals.layoutsCreated, totals.layoutHits);
    }

    // 所有键取自私有新页，发布后不再写 Shader.code 或状态参数；GPU 仅填充自己的 implementation。
    vsg::ref_ptr<vsg::GraphicsPipeline> pipeline(vsg::ref_ptr<vsg::GraphicsPipeline> source, OSGBColorSpace policy)
    {
        if (!source) return {};
        std::string key = std::to_string(static_cast<int>(policy)) + ':' + std::to_string(source->subpass) + ':';
        appendKey(key, valueKey(source->layout));
        for (const auto& state : source->pipelineStates) appendKey(key, valueKey(state));
        std::vector<std::string> shaderKeys;
        for (const auto& stage : source->stages)
        {
            if (!stage) throw std::runtime_error("OSGB pipeline has an empty shader stage");
            shaderKeys.push_back(stageKey(*stage));
            appendKey(key, shaderKeys.back());
        }
        auto found = pipelines.find(key);
        if (found != pipelines.end()) { ++totals.pipelineHits; return found->second; }
        source->layout = layout(source->layout);
        for (size_t i = 0; i < source->stages.size(); ++i)
        {
            auto& stage = source->stages[i];
            auto existing = modules.find(shaderKeys[i]);
            if (existing != modules.end()) { stage->module = existing->second; ++totals.shaderHits; continue; }
            if (stage->module->code.empty())
            {
                if (stage->module->source.empty()) throw std::runtime_error("OSGB shader has neither source nor SPIR-V");
                std::lock_guard<std::mutex> compilerLock(shaderCompilerMutex);
                Elapsed elapsed{totals.shaderCompileMilliseconds};
                auto compiler = vsg::ShaderCompiler::create();
                if (!compiler->supported() || !compiler->compile(stage) || stage->module->code.empty())
                    throw std::runtime_error("OSGB CPU shader precompilation failed");
            }
            intern(modules, shaderKeys[i], stage->module, totals.shaderModulesCreated, totals.shaderHits);
        }
        return intern(pipelines, key, source, totals.pipelinesCreated, totals.pipelineHits);
    }

    struct Process : vsg::Visitor
    {
        Impl& cache;
        OSGBColorSpace policy;
        std::set<vsg::Object*> visited;
        std::map<const vsg::ImageView*, vsg::ref_ptr<vsg::ImageView>> colorViews;
        Process(Impl& value, OSGBColorSpace p) : cache(value), policy(p) {}

        void apply(vsg::Object& object) override
        {
            if (visited.insert(&object).second)
            {
                // 转换器的后代容量预估不适用于按页编译；缓存读回也必须重新收敛。
                object.removeObject("ResourceHints");
                object.traverse(*this);
            }
        }
        void apply(vsg::PagedLOD& page) override
        {
            if (!visited.insert(&page).second) return;
            page.removeObject("ResourceHints");
            for (auto& child : page.children) if (child.node) child.node->accept(*this);
        }
        void apply(vsg::BindGraphicsPipeline& binding) override
        {
            if (visited.insert(&binding).second) binding.pipeline = cache.pipeline(binding.pipeline, policy);
        }
        void apply(vsg::BindDescriptorSet& binding) override
        {
            if (!visited.insert(&binding).second) return;
            binding.layout = cache.layout(binding.layout);
            if (binding.descriptorSet) binding.descriptorSet->accept(*this);
        }
        void apply(vsg::BindDescriptorSets& binding) override
        {
            if (!visited.insert(&binding).second) return;
            binding.layout = cache.layout(binding.layout);
            for (auto& set : binding.descriptorSets) if (set) set->accept(*this);
        }
        void apply(vsg::DescriptorSet& set) override
        {
            if (!visited.insert(&set).second) return;
            set.setLayout = cache.layout(set.setLayout);
            for (auto& descriptor : set.descriptors) if (descriptor) descriptor->accept(*this);
        }
        void apply(vsg::DescriptorImage& descriptor) override
        {
            if (!visited.insert(&descriptor).second) return;
            for (auto& info : descriptor.imageInfoList)
            {
                if (!info) continue;
                auto sampler = info->sampler;
                if (sampler) sampler = cache.intern(cache.samplers, valueKey(sampler), sampler,
                    cache.totals.samplersCreated, cache.totals.samplerHits);
                auto view = info->imageView;
                if (descriptor.dstBinding == 0 && view && view->image && view->image->data)
                {
                    const auto format = colorFormat(view->format, policy);
                    if (format != view->format || format != view->image->format ||
                        colorFormat(view->image->data->properties.format, policy) != view->image->data->properties.format)
                    {
                        auto found = colorViews.find(view.get());
                        if (found != colorViews.end()) view = found->second;
                        else
                        {
                            auto converted = colorView(*view, format, policy);
                            colorViews.emplace(view.get(), converted);
                            view = converted;
                            ++cache.totals.colorImagesConverted;
                        }
                    }
                }
                // ImageInfo 可能跨语义共享，始终替换描述符中的引用，不能原位污染数值贴图。
                info = vsg::ImageInfo::create(sampler, view, info->imageLayout);
            }
        }
    };
};

OSGBResources::OSGBResources() : _impl(std::make_unique<Impl>()) {}
OSGBResources::~OSGBResources() = default;

void OSGBResources::process(vsg::Node& node, OSGBColorSpace colorSpace)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    Elapsed elapsed{_impl->totals.resourceProcessMilliseconds};
    Impl::Process visitor(*_impl, colorSpace);
    node.accept(visitor);
}

OSGBResources::Statistics OSGBResources::statistics() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->totals;
}
}
