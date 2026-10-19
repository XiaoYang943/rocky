/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include "geostudio/native/globe/VisibleLayer.h"
#include "geostudio/native/globe/json.h"

using namespace geostudio::native;

#define LC "[VisibleLayer] \"" << getName() << "\" "

VisibleLayer::VisibleLayer() :
    super()
{
    construct({});
}

VisibleLayer::VisibleLayer(std::string_view conf) :
    super(conf)
{
    construct(conf);
}

void
VisibleLayer::construct(std::string_view conf)
{
    const auto j = parse_json(conf);
    get_to(j, "opacity", opacity);
}

std::string
VisibleLayer::to_json() const
{
    auto j = parse_json(super::to_json());
    set(j, "opacity", opacity);
    return j.dump();
}
