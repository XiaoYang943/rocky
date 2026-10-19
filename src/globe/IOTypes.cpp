/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include "geostudio/native/globe/IOTypes.h"
#include "geostudio/native/globe/Context.h"
#include "geostudio/native/globe/json.h"

using namespace geostudio::native;

IOOptions::IOOptions()
{
    _services = std::make_shared<Services>();
}

Services::Services()
{
    readImageFromURI = [](const std::string& location, const IOOptions&) { 
        return Failure(Failure::ServiceUnavailable, "Services.readImageFromURI is not implemented"); };

    readImageFromStream = [](std::istream& stream, std::string contentType, const IOOptions& io) {
        return Failure(Failure::ServiceUnavailable, "Services.readImageFromStream is not implemented"); };
}
