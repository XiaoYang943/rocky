/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/scene/entity/ProjectedTexture.h>
#include <geostudio/native/render/entity/ECSNode.h>
#include <geostudio/native/render/entity/TextureResource.h>
#include <geostudio/native/render/entity/Texture.h>
#include <mutex>
#include <vector>

namespace geostudio::native
{
    /** Publishes Texture and ImageTexture components as shared renderer resources. */
    class GEOSTUDIO_NATIVE_API TextureSystemNode :
        public vsg::Inherit<detail::SimpleSystemNodeBase, TextureSystemNode>
    {
    public:
        //! Installs producer lifecycle hooks and adopts already-existing texture sources.
        TextureSystemNode(Registry& registry);
        //! Disconnects lifecycle hooks under the registry write lock before callback-owned members are destroyed.
        ~TextureSystemNode() override;
        //! Publishes changed sources and retires replaced GPU references on the update thread.
        void update(VSGContext) override;

    private:
        // Resource removal can occur on a paging thread. Retain producer-owned
        // images until update() can hand them to the deferred GPU disposer.
        std::mutex _pendingDisposalsMutex;
        std::vector<vsg::ref_ptr<vsg::ImageInfo>> _pendingDisposals;

        //! Registers a native source; publication is deferred until update().
        void on_construct_Texture(entt::registry&, entt::entity);
        //! Invalidates native source contents after registry.patch/emplace_or_replace.
        void on_update_Texture(entt::registry&, entt::entity);
        //! Removes only this source's published resource; safe on a paging thread.
        void on_destroy_Texture(entt::registry&, entt::entity);
        //! Registers an image source; publication is deferred until update().
        void on_construct_ImageTexture(entt::registry&, entt::entity);
        //! Invalidates image contents after registry.patch/emplace_or_replace.
        void on_update_ImageTexture(entt::registry&, entt::entity);
        //! Removes only this source's published resource; safe on a paging thread.
        void on_destroy_ImageTexture(entt::registry&, entt::entity);
        //! Queues references owned by this system for deferred GPU retirement.
        void on_destroy_TextureResource(entt::registry&, entt::entity);
    };
}

EVSG_type_name(geostudio::native::TextureSystemNode)
