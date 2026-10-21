/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/base/Log.h>
#include <geostudio/native/drivers/gdal/GDALImageLayer.h>
#include <geostudio/native/drivers/gdal/GDALElevationLayer.h>
#include <geostudio/native/drivers/tms/TMSImageLayer.h>
#include <geostudio/native/drivers/tms/TMSElevationLayer.h>
#ifdef GEOSTUDIO_NATIVE_HAS_MBTILES
#include <geostudio/native/drivers/mbtiles/MBTilesImageLayer.h>
#include <geostudio/native/drivers/mbtiles/MBTilesElevationLayer.h>
#endif
#ifdef GEOSTUDIO_NATIVE_HAS_AZURE
#include <geostudio/native/drivers/azure/AzureImageLayer.h>
#endif
#include <geostudio/native/drivers/gdal/GDALFeatureSource.h>
#include <geostudio/native/drivers/earth/EarthFileImporter.h>
#include <geostudio/native/scene/entity/ECS.h>

#ifdef GEOSTUDIO_NATIVE_HAS_VSG
#include <geostudio/native/runtime/engine/Application.h>
#include <geostudio/native/render/view/MapManipulator.h>
#include <geostudio/native/render/vsg/NodeLayer.h>
#include <geostudio/native/render/paging/NodePager.h>
#include <geostudio/native/render/vsg/GeoTransform.h>
#include <geostudio/native/render/vsg/PixelScaleTransform.h>
#include <geostudio/native/render/vsg/ViewDependentState.h>
#include <geostudio/native/render/entity/FeatureBuilder.h>
#include <geostudio/native/render/entity/EntityNode.h>
#include <geostudio/native/render/entity/ECSTypes.h>
#include <geostudio/native/render/entity/ECSVisitors.h>
#include <geostudio/native/render/entity/ECSNode.h>
#include <geostudio/native/render/view/ECSIntersector.h>
#endif

// 此入口只导出与宿主 UI 无关的 SDK 能力。