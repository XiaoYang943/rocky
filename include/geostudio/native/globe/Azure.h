/**
 * rocky c++
 * Copyright 2023-2024 Pelican Mapping, Chris Djali
 * MIT License
 */
#pragma once

#include <geostudio/native/globe/Version.h>
#ifdef ROCKY_HAS_AZURE

#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/option.h>
#include <geostudio/native/globe/URI.h>

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

#else // if !ROCKY_HAS_AZURE
  #ifndef ROCKY_BUILDING_SDK
    #error Azure support is not enabled in Rocky.
  #endif
#endif // ROCKY_HAS_AZURE
