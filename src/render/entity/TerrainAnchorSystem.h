/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/core/base/Callbacks.h>
#include <geostudio/native/core/geo/tiling/TileKey.h>
#include <geostudio/native/scene/entity/TerrainAnchor.h>
#include <geostudio/native/render/entity/System.h>
#include <mutex>
#include <unordered_set>

namespace geostudio::native
{
    class TerrainNode;

    /**
     * Resolves TerrainAnchor components and writes the result into Transform.
     */
    class GEOSTUDIO_NATIVE_API TerrainAnchorSystem : public System
    {
    public:
        //! Constructs the system and installs TerrainAnchor lifecycle hooks.
        TerrainAnchorSystem(Registry& registry);

        //! Disconnects loader-thread terrain callbacks before cache state is destroyed.
        ~TerrainAnchorSystem();

        //! Loaded terrain used to resolve anchor heights.
        vsg::observer_ptr<TerrainNode> target;

        //! Resolves changed anchors before TransformSystem synchronizes them.
        void update(VSGContext vsgcontext) override;

    private:
        vsg::observer_ptr<TerrainNode> _subscribedTarget;
        CallbackSubs _terrainSubscriptions;
        bool _targetChanged = true;
        std::mutex _loadedTilesMutex;
        std::unordered_set<TileKey> _loadedTiles;

        //! Updates the terrain activity subscription when the target changes.
        void updateTargetSubscription();

        //! Creates internal cache state for a newly added anchor.
        void on_construct_TerrainAnchor(entt::registry& registry, entt::entity entity);

        //! Removes internal cache state owned by a removed anchor.
        void on_destroy_TerrainAnchor(entt::registry& registry, entt::entity entity);
    };
}
