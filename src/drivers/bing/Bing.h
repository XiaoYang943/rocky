/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once

#include <geostudio/native/core/base/Version.h>
#ifdef GEOSTUDIO_NATIVE_HAS_BING

#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/base/option.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    namespace Bing
    {
        struct ImageLayerOptions
        {
            option<std::string> apiKey;
            option<std::string> imagerySet = { "Aerial" };
            option<URI> imageryMetadataUrl = URI("https://dev.virtualearth.net/REST/v1/Imagery/Metadata");
        };

        struct ElevationLayerOptions
        {
            option<std::string> apiKey;
            option<URI> url = URI("http://dev.virtualearth.net/REST/v1/Elevation/Bounds");
        };
    }
}

#else // if !GEOSTUDIO_NATIVE_HAS_BING
  #ifndef GEOSTUDIO_NATIVE_BUILDING_SDK
    #error Bing support is not enabled in Rocky.
  #endif
#endif // GEOSTUDIO_NATIVE_HAS_BING
