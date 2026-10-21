/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/base/Result.h>
#include <geostudio/native/core/io/IOTypes.h>
#include <vector>

namespace geostudio::native
{
    class Feature;

    class GEOSTUDIO_NATIVE_API Geocoder
    {
    public:
        Geocoder() = default;

        Result<std::vector<Feature>> geocode(const std::string& location, IOOptions& io) const;
    };
}
