/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/Version.h>
#ifdef ROCKY_HAS_BING

#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/option.h>
#include <geostudio/native/globe/URI.h>

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

#else // if !ROCKY_HAS_BING
  #ifndef ROCKY_BUILDING_SDK
    #error Bing support is not enabled in Rocky.
  #endif
#endif // ROCKY_HAS_BING
