/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/vsg/Common.h>
#include <geostudio/native/globe/TileKey.h>

namespace geostudio::native
{
    class TerrainTileNode;
    class TerrainTilePager;
    class TerrainSettings;


    /**
     * Activity interface for getting terrain runtime information and metrics
     */
    class TerrainActivity
    {
    public:
        Callback<const TileKey> onTileLoaded;

        //! Resident surface/hierarchy changes, including page-out. An invalid key invalidates the whole terrain.
        //! Fired at scene-update safe points; subscribers must not modify the terrain in the callback.
        Callback<const TileKey> onTileBoundsChanged;
    };


    /** 
     * Interface for terrain tiles to notify their host of their active state.
     */
    class TerrainTileHost
    {
    public:
        //! Tell the host that a tile is alive!
        virtual void ping(TerrainTileNode* tile, const TerrainTileNode* parent, vsg::RecordTraversal& t) = 0;

        //! Access terrain settings.
        virtual const TerrainSettings& settings() const = 0;

        //! Access terrain activity interface.
        virtual TerrainActivity& activity() = 0;

        //! Access the tile pager.
        virtual TerrainTilePager& tiles() = 0;
    };
}
