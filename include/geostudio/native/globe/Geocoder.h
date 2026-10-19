/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/globe/Common.h>
#include <geostudio/native/globe/Result.h>
#include <geostudio/native/globe/IOTypes.h>
#include <vector>

namespace geostudio::native
{
    class Feature;

    class ROCKY_EXPORT Geocoder
    {
    public:
        Geocoder() = default;

        Result<std::vector<Feature>> geocode(const std::string& location, IOOptions& io) const;
    };
}
