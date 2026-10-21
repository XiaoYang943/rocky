/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once

#include <geostudio/native/core/base/Version.h>
#ifdef GEOSTUDIO_NATIVE_HAS_AZURE

#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/base/option.h>
#include <geostudio/native/core/io/URI.h>

namespace geostudio::native
{
    namespace Azure
    {
        //! Options you can pass to the AzureImageLayer.
        struct Options
        {
            //! Azure Maps subscription key
            //! Or, use the AZURE_KEY environment variable
            option<std::string> subscriptionKey;

            //! Tileset ID
            //! See https://learn.microsoft.com/en-us/rest/api/maps/render/get-map-tile?view=rest-maps-2024-04-01&tabs=HTTP#tilesetid
            option<std::string> tilesetId = { "microsoft.base.road" };

            //! Base URL for the Azure Maps tile API
            option<URI> mapTileApiUrl = URI("https://atlas.microsoft.com/map/tile");

            //! API version
            option<std::string> apiVersion = { "2024-04-01" };
        };
    }
}

#else // if !GEOSTUDIO_NATIVE_HAS_AZURE
  #ifndef GEOSTUDIO_NATIVE_BUILDING_SDK
    #error Azure support is not enabled in Rocky.
  #endif
#endif // GEOSTUDIO_NATIVE_HAS_AZURE
