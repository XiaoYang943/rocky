/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/scene/entity/Decal.h>
#include <geostudio/native/scene/entity/Overlay.h>
#include <geostudio/native/scene/entity/ProjectedTexture.h>
#include <geostudio/native/render/entity/ECSNode.h>
#include <geostudio/native/render/entity/TextureResource.h>

namespace geostudio::native
{
    class View;

    namespace detail
    {
        struct VectorOverlayPickView;

        /**
         * Consumer-side residency for a TextureResource, not image ownership.
         * Removing this component queues its slot for release during update.
         * Publication waits one frame for terrain's descriptor-set rebuild,
         * just like Vector atlas slots; the arena's CPU update alone is not
         * sufficient to expose the new texture index to a draw.
         */
        struct TextureSlotDetail
        {
            vsg::ref_ptr<vsg::ImageInfo> texture;
            std::int32_t descriptorImageIndex = -1;
            std::uint64_t descriptorWriteFrame = 0u;
            bool readyForDraw = false;
        };
    }

    /**
     * Normalizes decal facades and publishes projected payloads to the GPU.
     *
     * Decal, DecalStyle, and Overlay are adapted into the lower-level
     * ProjectedTexture and texture-source components. The system then assigns
     * visible TextureResource or SlugResource payloads to bounded global
     * descriptor arrays, writes per-view DecalGPU/SlugLayerGPU records, and
     * dispatches the compute shader that builds each screen tile's decal list.
     */
    class GEOSTUDIO_NATIVE_API DecalSystemNode : public vsg::Inherit<detail::SimpleSystemNodeBase, DecalSystemNode>
    {
    public:
        //! Construct the system
        DecalSystemNode(Registry& registry);

        //! Draw receiving-volume edges for all submitted decals, independently of cell-list limits.
        //! Change these settings during update; disabled debug drawing records no graphics commands.
        bool debugVolumes = false;
        bool debugVolumesSeeThrough = true;

        //! Renderer-owned graphics node; attach once to the main scene, not the compute graph.
        vsg::ref_ptr<vsg::Node> debugNode() const { return _debugNode; }

    public: // SimpleSystemNodeBase
        void initialize(VSGContext) override;
        void update(VSGContext) override;

    public: // vsg::Object
        void traverse(vsg::RecordTraversal&) const override;
        void traverse(vsg::ConstVisitor&) const override;
        void traverse(vsg::Visitor&) override;

    private:
        friend class ECSIntersector;

        //! Internal receiver-coverage query for ECSIntersector; returns an instance or null.
        //! Requires the application thread without a registry lock; ignores scene/raster occlusion.
        entt::entity intersectVectorOverlay(View&, int x, int y, float minAlpha);

        //! Per-view compute commands. The commands cull logical decals into
        //! that view's screen-tile lists before terrain fragments consume them.
        struct ViewDetail
        {
            vsg::ref_ptr<vsg::Commands> commands;
            std::shared_ptr<detail::VectorOverlayPickView> picks;
        };
        mutable ViewLocal<ViewDetail> _views;
        std::shared_ptr<SharedRenderData> _sharedRenderData;
        vsg::ref_ptr<vsg::Node> _debugNode;

        vsg::ref_ptr<vsg::ShaderStage> _cullingShader;

        // Descriptor slots whose owning style component was removed. They are
        // released during update(), where the shared descriptor arena is available.
        std::mutex _pendingTextureSlotsMutex;
        std::vector<std::int32_t> _pendingTextureSlots;
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        std::vector<std::int32_t> _pendingSlugSlots;
#endif

        // Kept outside the descriptor arena so slot zero remains usable.
        vsg::ref_ptr<vsg::ImageInfo> _fallbackTexture;
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        vsg::ref_ptr<vsg::ImageInfo> _fallbackSlugCurve;
        vsg::ref_ptr<vsg::ImageInfo> _fallbackSlugBand;
#endif

        // Prevent descriptor-capacity warnings from flooding the log every frame.
        bool _textureSlotsExhausted = false;
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        bool _slugSlotsExhausted = false;
#endif

        void rebuildCommands(ViewIDType, VSGContext);
        void updateStyles(VSGContext);
        void resizeGPUBuffersIfNeeded(VSGContext);
        void updateDecalsSSBO(VSGContext);

        void on_construct_Decal(entt::registry& r, entt::entity e);
        void on_construct_DecalStyle(entt::registry& r, entt::entity e);
        void on_construct_Overlay(entt::registry& r, entt::entity e);
        void on_construct_ProjectedTexture(entt::registry& r, entt::entity e);
        void on_construct_TextureResource(entt::registry& r, entt::entity e);
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        void on_construct_SlugResource(entt::registry& r, entt::entity e);
#endif
        void on_destroy_Decal(entt::registry& r, entt::entity e);
        void on_destroy_DecalStyle(entt::registry& r, entt::entity e);
        void on_destroy_Overlay(entt::registry& r, entt::entity e);
        void on_destroy_ProjectedTexture(entt::registry& r, entt::entity e);
        void on_destroy_TextureResource(entt::registry& r, entt::entity e);
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        void on_destroy_SlugResource(entt::registry& r, entt::entity e);
#endif
        void on_destroy_TextureSlotDetail(entt::registry& r, entt::entity e);
#ifdef GEOSTUDIO_NATIVE_HAS_SLUGHORN
        void on_destroy_SlugSlotDetail(entt::registry& r, entt::entity e);
#endif
        void on_update_Decal(entt::registry& r, entt::entity e);
        void on_update_DecalStyle(entt::registry& r, entt::entity e);
        void on_update_Overlay(entt::registry& r, entt::entity e);
    };
}

EVSG_type_name(geostudio::native::DecalSystemNode)
