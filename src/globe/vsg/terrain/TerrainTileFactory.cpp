/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include "geostudio/native/globe/vsg/terrain/TerrainTileFactory.h"
#include "geostudio/native/globe/vsg/terrain/TerrainTilePager.h"
#include "geostudio/native/globe/vsg/terrain/TerrainTileHost.h"
#include "geostudio/native/globe/vsg/terrain/TerrainSettings.h"
#include "geostudio/native/globe/vsg/terrain/TerrainState.h"
#include "geostudio/native/globe/vsg/terrain/SurfaceNode.h"
#include "geostudio/native/globe/vsg/terrain/GeometryPool.h"
#include <geostudio/native/globe/TerrainTileModel.h>
#include <geostudio/native/globe/Map.h>

using namespace geostudio::native;


TerrainTileFactory::TerrainTileFactory(std::shared_ptr<const Map> new_map, const Profile& new_profile,
    const SRS& new_renderingSRS, std::shared_ptr<TerrainState> new_state,
    VSGContext vsgcontext, const TerrainSettings& new_settings, TerrainTileHost* new_host) :

    map(new_map),
    profile(new_profile),
    renderingSRS(new_renderingSRS),
    state(new_state),
    settings(new_settings),
    geometryPool(new_renderingSRS),
    host(new_host)
{
    ROCKY_SOFT_ASSERT(map, "Map is required");
    ROCKY_SOFT_ASSERT(profile.valid(), "Valid profile required");


    vsgcontext->io.services().jobs.get_pool(loadSchedulerName)->set_concurrency(settings.concurrency);

    // geometry pooling not supported for QSC yet.
    if (new_profile.srs().isQSC())
    {
        geometryPool.enabled = false;
    }
}

TerrainTileFactory::~TerrainTileFactory()
{
#ifdef ROCKY_DEBUG_MEMCHECK
    Log()->debug("~TerrainTileFactory");
#endif
}

vsg::ref_ptr<TerrainTileNode>
TerrainTileFactory::createTile(const TileKey& key, vsg::ref_ptr<TerrainTileNode> parent, VSGContext context)
{
    GeometryPool::Settings geomSettings
    {
        settings.tileSize,
        settings.skirtRatio,
        false // morphing
    };

    // Get a shared geometry from the pool that corresponds to this tile key:
    auto geometry = geometryPool.getPooledGeometry(key, geomSettings, nullptr);

    // Make the new terrain tile
    auto tile = TerrainTileNode::create();
    tile->key = key;
    tile->renderModel.descriptors = state->defaultTileDescriptors;
    tile->doNotExpire = (parent == nullptr);
    tile->stategroup = vsg::StateGroup::create();
    tile->stategroup->addChild(geometry);
    tile->surface = SurfaceNode::create(key, renderingSRS);
    tile->surface->addChild(tile->stategroup);
    tile->addChild(tile->surface);
    tile->host = host;

    // inherit model data from the parent
    if (parent)
        tile->inheritFrom(parent);

    // update the bounding sphere for culling
    tile->bound = tile->surface->recomputeBound();

    // Generate its state objects:
    tile->renderModel = state->updateRenderModel(tile->key, tile->renderModel, {}, context);

    // install the bind command.
    tile->stategroup->add(tile->renderModel.descriptors.bind);

    return tile;
}

bool TerrainTileFactory::update(VSGContext vc)
{
    bool changes = false;

    geometryPool.sweep(vc);

    auto pool = vc->io.services().jobs.get_pool(loadSchedulerName);

    if (pool->concurrency() != settings.concurrency)
    {
        pool->set_concurrency(settings.concurrency);
        changes = true;
    }

    return changes;
}
