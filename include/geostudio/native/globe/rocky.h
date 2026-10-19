/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/Log.h>
#include <geostudio/native/globe/GDALImageLayer.h>
#include <geostudio/native/globe/GDALElevationLayer.h>
#include <geostudio/native/globe/TMSImageLayer.h>
#include <geostudio/native/globe/TMSElevationLayer.h>
#ifdef ROCKY_HAS_MBTILES
#include <geostudio/native/globe/MBTilesImageLayer.h>
#include <geostudio/native/globe/MBTilesElevationLayer.h>
#endif
#ifdef ROCKY_HAS_AZURE
#include <geostudio/native/globe/AzureImageLayer.h>
#endif
#include <geostudio/native/globe/GDALFeatureSource.h>
#include <geostudio/native/globe/contrib/EarthFileImporter.h>
#include <geostudio/native/globe/ECS.h>

#ifdef ROCKY_HAS_VSG
#include <geostudio/native/globe/vsg/Application.h>
#include <geostudio/native/globe/vsg/MapManipulator.h>
#include <geostudio/native/globe/vsg/NodeLayer.h>
#include <geostudio/native/globe/vsg/NodePager.h>
#include <geostudio/native/globe/vsg/GeoTransform.h>
#include <geostudio/native/globe/vsg/PixelScaleTransform.h>
#include <geostudio/native/globe/vsg/ViewDependentState.h>
#include <geostudio/native/globe/vsg/ecs/FeatureBuilder.h>
#include <geostudio/native/globe/vsg/ecs/EntityNode.h>
#include <geostudio/native/globe/vsg/ecs/ECSTypes.h>
#include <geostudio/native/globe/vsg/ecs/ECSVisitors.h>
#include <geostudio/native/globe/vsg/ecs/ECSNode.h>
#include <geostudio/native/globe/vsg/ecs/ECSIntersector.h>
#include <geostudio/native/globe/vsg/ecs/WidgetSystem.h>
#endif

// NOTE: do NOT add any imgui-related includes here