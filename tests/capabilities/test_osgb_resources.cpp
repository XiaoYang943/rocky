#include "../globe/catch.hpp"
#include "../../src/render/import/osgb/OSGBResources.h"
#include <vsg/all.h>
#include <array>
#include <cstring>
#include <exception>
#include <thread>

using geostudio::native::io::detail::OSGBColorSpace;
using geostudio::native::io::detail::OSGBResources;

namespace
{
struct ResourcePage
{
    vsg::ref_ptr<vsg::StateGroup> node;
    vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline;
    vsg::ref_ptr<vsg::BindDescriptorSet> descriptors;
    vsg::ref_ptr<vsg::DescriptorImage> color, numeric;
};

// 构建无窗口、无 Device 的私有页；两种纹理语义刻意共享 ImageInfo，验证适配不污染数据贴图。
ResourcePage page(vsg::ref_ptr<vsg::Data> data = {}, bool variant = false, bool blending = false)
{
    ResourcePage result;
    result.node = vsg::StateGroup::create();
    if (!data)
    {
        auto rgba = vsg::ubvec4Array2D::create(2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
        for (auto& value : *rgba) value = vsg::ubvec4(64, 128, 192, 73);
        data = rgba;
    }
    auto sampler = vsg::Sampler::create();
    auto sharedInfo = vsg::ImageInfo::create(sampler, data);
    result.color = vsg::DescriptorImage::create(sharedInfo, 0);
    result.numeric = vsg::DescriptorImage::create(sharedInfo, 5);
    vsg::DescriptorSetLayoutBindings bindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    auto setLayout = vsg::DescriptorSetLayout::create(bindings);
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{setLayout}, vsg::PushConstantRanges{});
    auto settings = vsg::ShaderCompileSettings::create();
    if (variant) settings->defines.insert("TEST_VARIANT");
    auto vertex = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", std::string(
        "#version 450\nvoid main() { gl_Position = vec4(0, 0, 0, 1); }\n"));
    auto fragment = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", std::string(
        "#version 450\n#pragma import_defines(TEST_VARIANT)\n"
        "layout(location=0) out vec4 color;\nvoid main() {\n"
        "#ifdef TEST_VARIANT\ncolor = vec4(0.5);\n#else\ncolor = vec4(1.0);\n#endif\n}\n"), settings);
    auto blend = vsg::ColorBlendState::create();
    blend->attachments.front().blendEnable = blending ? VK_TRUE : VK_FALSE;
    auto pipeline = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vertex, fragment},
        vsg::GraphicsPipelineStates{vsg::VertexInputState::create(), vsg::InputAssemblyState::create(),
            vsg::RasterizationState::create(), vsg::MultisampleState::create(), blend, vsg::DepthStencilState::create()});
    result.pipeline = vsg::BindGraphicsPipeline::create(pipeline);
    auto descriptorSet = vsg::DescriptorSet::create(setLayout, vsg::Descriptors{result.color, result.numeric});
    result.descriptors = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptorSet);
    result.node->add(result.pipeline);
    result.node->add(result.descriptors);
    return result;
}

vsg::ImageView& colorImage(ResourcePage& item) { return *item.color->imageInfoList.front()->imageView; }
}

TEST_CASE("OSGB diffuse color policy separates numeric texture semantics", "[osgb][resources]")
{
    OSGBResources cache;
    auto sample = page();
    auto original = colorImage(sample).image->data;
    auto raw = original->dataPointer();
    auto originalInfo = sample.numeric->imageInfoList.front();
    cache.process(*sample.node);
    auto& color = colorImage(sample);
    CHECK(color.format == VK_FORMAT_R8G8B8A8_SRGB);
    CHECK(color.image->format == color.format);
    CHECK(color.image->data->properties.format == color.format);
    CHECK(color.image->data.get() != original.get());
    CHECK(color.image->data->dataPointer() == raw);
    CHECK(original->properties.format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(sample.numeric->imageInfoList.front()->imageView->format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(originalInfo->imageView->format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(static_cast<const uint8_t*>(color.image->data->dataPointer())[3] == 73);
    CHECK(cache.statistics().colorImagesConverted == 1);

    auto linear = page();
    cache.process(*linear.node, OSGBColorSpace::Linear);
    CHECK(colorImage(linear).format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(linear.pipeline->pipeline != sample.pipeline->pipeline);
    CHECK(linear.pipeline->pipeline->stages.front()->module == sample.pipeline->pipeline->stages.front()->module);
}

TEST_CASE("OSGB runtime pages discard speculative descendant descriptor reservations", "[osgb][resources][memory]")
{
    OSGBResources cache;
    auto sample = page();
    auto hints = vsg::ResourceHints::create();
    hints->numDescriptorSets = 1025;
    hints->descriptorPoolSizes = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2050}};
    sample.node->setObject("ResourceHints", hints);
    auto root = vsg::PagedLOD::create();
    root->children[1].node = sample.node;
    root->setObject("ResourceHints", hints);
    cache.process(*root);
    vsg::CollectResourceRequirements count;
    root->accept(count);
    CHECK(count.requirements.computeNumDescriptorSets() == 1);
    REQUIRE(count.requirements.computeDescriptorPoolSizes().size() == 1);
    CHECK(count.requirements.computeDescriptorPoolSizes().front().descriptorCount == 2);
    CHECK(count.requirements.containsPagedLOD);
    CHECK_FALSE(root->getObject("ResourceHints"));
    CHECK_FALSE(sample.node->getObject("ResourceHints"));
}

TEST_CASE("OSGB color policy preserves RGB expansion and compressed mip payloads", "[osgb][resources]")
{
    OSGBResources cache;
    auto rgb = vsg::ubvec3Array2D::create(2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8_UNORM});
    auto rgbPage = page(rgb);
    cache.process(*rgbPage.node);
    CHECK(colorImage(rgbPage).format == VK_FORMAT_R8G8B8A8_SRGB);
    CHECK(colorImage(rgbPage).image->data->properties.format == VK_FORMAT_R8G8B8_SRGB);
    CHECK(colorImage(rgbPage).image->data->dataSize() == rgb->dataSize());

    const std::pair<VkFormat, VkFormat> formats[] = {
        {VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC1_RGB_SRGB_BLOCK},
        {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
        {VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK},
        {VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK}};
    for (const auto& format : formats)
    {
        vsg::Data::Properties properties{format.first};
        properties.blockWidth = properties.blockHeight = 4;
        properties.mipLevels = 3;
        // 显式 storage 覆盖 4x4、2x2、1x1 三层块，避免构造器仅分配首层。
        const bool small = format.first == VK_FORMAT_BC1_RGB_UNORM_BLOCK || format.first == VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        auto storage = vsg::ubyteArray::create(21 * (small ? 8 : 16));
        std::memset(storage->dataPointer(), 0x55, storage->dataSize());
        vsg::ref_ptr<vsg::Data> blocks;
        if (small) blocks = vsg::block64Array2D::create(storage, 0, 8, 4, 4, properties);
        else blocks = vsg::block128Array2D::create(storage, 0, 16, 4, 4, properties);
        auto compressed = page(blocks);
        cache.process(*compressed.node);
        auto& output = colorImage(compressed);
        CHECK(output.format == format.second);
        CHECK(output.image->format == format.second);
        CHECK(output.image->data->properties.format == format.second);
        CHECK(output.image->data->properties.mipLevels == 3);
        CHECK(output.image->data->dataSize() == blocks->dataSize());
        CHECK(output.image->data->dataPointer() == blocks->dataPointer());
        CHECK(output.image->extent.width == 16);
    }
}

TEST_CASE("OSGB resources share stable variants across pages after CPU compilation", "[osgb][resources]")
{
    OSGBResources cache;
    auto first = page();
    auto second = page();
    cache.process(*first.node);
    REQUIRE_FALSE(first.pipeline->pipeline->stages.front()->module->code.empty());
    cache.process(*second.node);
    CHECK(first.pipeline->pipeline == second.pipeline->pipeline);
    CHECK(first.descriptors->layout == second.descriptors->layout);
    CHECK(first.descriptors->descriptorSet != second.descriptors->descriptorSet);
    CHECK(first.descriptors->descriptorSet->setLayout == second.descriptors->descriptorSet->setLayout);
    CHECK(first.color->imageInfoList.front()->sampler == second.color->imageInfoList.front()->sampler);
    CHECK(first.color->imageInfoList.front()->imageView != second.color->imageInfoList.front()->imageView);
    CHECK(cache.statistics().pipelinesCreated == 1);
    CHECK(cache.statistics().pipelineHits == 1);
    CHECK(cache.statistics().shaderModulesCreated == 2);

    auto blending = page({}, false, true);
    cache.process(*blending.node);
    CHECK(blending.pipeline->pipeline != first.pipeline->pipeline);
    CHECK(blending.pipeline->pipeline->stages.front()->module == first.pipeline->pipeline->stages.front()->module);
    auto variant = page({}, true);
    cache.process(*variant.node);
    CHECK(variant.pipeline->pipeline != first.pipeline->pipeline);
    CHECK(variant.pipeline->pipeline->stages.back()->module != first.pipeline->pipeline->stages.back()->module);
    CHECK(cache.statistics().pipelinesCreated == 3);
    CHECK(cache.statistics().shaderModulesCreated == 3);
    CHECK(cache.statistics().shaderCompileMilliseconds > 0.0);
    CHECK(cache.statistics().resourceProcessMilliseconds >= cache.statistics().shaderCompileMilliseconds);
}

TEST_CASE("OSGB resource cold misses are atomic across concurrent private pages", "[osgb][resources]")
{
    OSGBResources cache;
    std::array<ResourcePage, 8> pages;
    std::array<std::exception_ptr, 8> errors{};
    std::array<std::thread, 8> threads;
    for (size_t i = 0; i < pages.size(); ++i) pages[i] = page();
    for (size_t i = 0; i < pages.size(); ++i)
        threads[i] = std::thread([&, i] { try { cache.process(*pages[i].node); } catch (...) { errors[i] = std::current_exception(); } });
    for (auto& thread : threads) thread.join();
    for (size_t i = 0; i < pages.size(); ++i)
    {
        REQUIRE_FALSE(errors[i]);
        CHECK(pages[i].pipeline->pipeline == pages.front().pipeline->pipeline);
        CHECK(colorImage(pages[i]).format == VK_FORMAT_R8G8B8A8_SRGB);
    }
    CHECK(cache.statistics().pipelinesCreated == 1);
    CHECK(cache.statistics().pipelineHits == pages.size() - 1);
    CHECK(cache.statistics().shaderModulesCreated == 2);
    CHECK(cache.statistics().samplersCreated == 1);
}

TEST_CASE("OSGB resource traversal keeps external pages lazy", "[osgb][resources]")
{
    OSGBResources cache;
    auto coarse = page();
    auto root = vsg::PagedLOD::create();
    root->filename = "unread-fine.osgb";
    root->children[1].node = coarse.node;
    cache.process(*root);
    CHECK_FALSE(root->children[0].node);
    CHECK(root->filename == vsg::Path("unread-fine.osgb"));
    CHECK(cache.statistics().pipelinesCreated == 1);
    CHECK(colorImage(coarse).format == VK_FORMAT_R8G8B8A8_SRGB);
}

TEST_CASE("OSGB shader precompilation reports failure without caching bad pipeline", "[osgb][resources]")
{
    OSGBResources cache;
    auto broken = page();
    broken.pipeline->pipeline->stages.back()->module->source = "#version 450\ninvalid shader source\n";
    CHECK_THROWS_WITH(cache.process(*broken.node), "OSGB CPU shader precompilation failed");
    CHECK(cache.statistics().pipelinesCreated == 0);
    auto valid = page();
    CHECK_NOTHROW(cache.process(*valid.node));
    CHECK(cache.statistics().pipelinesCreated == 1);
}
