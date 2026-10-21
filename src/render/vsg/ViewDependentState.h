/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/render/vsg/Common.h>

namespace geostudio::native
{
    class MapNode;

    /**
    * Extends vsg::ViewDependentState to add data for Rocky rendering.
    *
    * Note! Even though we replace each vsg::View's VDS with this one, VSG will still make its OWN
    * View's with stock ViewDependentState objects for shadows (and perhaps other things).
    * So we need to be careful to only use this class when we know it's ours, and not a stock VDS.
    */
    class GEOSTUDIO_NATIVE_API ViewDependentStateEx : public vsg::Inherit<vsg::ViewDependentState, ViewDependentStateEx>
    {
    public:
        ViewDependentStateEx(vsg::ref_ptr<vsg::View> vsgView, vsg::ref_ptr<vsg::Device> device);

        mutable vsg::ref_ptr<vsg::DescriptorBuffer> renderParamsBuf;
        vsg::ref_ptr<vsg::DescriptorBuffer> frustumParamsBuf;
        vsg::ref_ptr<vsg::DescriptorBuffer> frustumsBuf;

        // DecalSystem writes logical projection records to decalsBuf. Its
        // compute pass fills decalTilesBuf with the small list relevant to each
        // screen tile, avoiding an all-decals loop in every terrain fragment.
        vsg::ref_ptr<vsg::DescriptorBuffer> decalsBuf;
        vsg::ref_ptr<vsg::DescriptorBuffer> decalTilesBuf;
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        // Flattened metadata for the Slug shapes referenced by this view's
        // DecalGPU ranges. Atlas textures themselves live in SharedRenderData.
        vsg::ref_ptr<vsg::DescriptorBuffer> slugLayersBuf;
#endif

        Revision revision = 0;

        vsg::ref_ptr<vsg::Device> device() { return _device; }

    public:
        void init(vsg::ResourceRequirements& req) override;

        void traverse(vsg::RecordTraversal& rt) const override;

    protected:
        mutable vsg::observer_ptr<MapNode> _mapNode;
        vsg::ref_ptr<vsg::Device> _device;
    };
    

    //! Convenience function that adds the VDS descriptor bindings to a shader set.
    extern GEOSTUDIO_NATIVE_API void addViewDependentStateToShaderSet(
        vsg::ShaderSet* shaderSet,
        VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL);


    //! Convenience function that enables VDS uniforms on a pipeline.
    extern GEOSTUDIO_NATIVE_API void enableViewDependentStateUniforms(
        vsg::GraphicsPipelineConfigurator* gpc);


    //! Retrieve the VDS from a view.
    inline ViewDependentStateEx* viewDependentState(vsg::View* view)
    {
        if (view)
            return dynamic_cast<ViewDependentStateEx*>(view->viewDependentState.get());
        else
            return nullptr;
    }
}
