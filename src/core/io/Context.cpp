/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/core/io/Context.h>
#include <geostudio/native/core/geo/GeoExtent.h>
#include <geostudio/native/core/geo/SRS.h>
#include <geostudio/native/core/io/Utils.h>
#include <geostudio/native/core/base/Version.h>
#include <geostudio/native/core/io/json.h>
#include <geostudio/native/core/async/weejobs.h>

GEOSTUDIO_NATIVE_ABOUT(rocky, GEOSTUDIO_NATIVE_VERSION_STRING)
GEOSTUDIO_NATIVE_ABOUT(weejobs, WEEJOBS_VERSION_STRING)
GEOSTUDIO_NATIVE_ABOUT(glm, std::to_string(GLM_VERSION_MAJOR) + "." + std::to_string(GLM_VERSION_MINOR) + "." + std::to_string(GLM_VERSION_PATCH) + "." + std::to_string(GLM_VERSION_REVISION))
GEOSTUDIO_NATIVE_ABOUT(nlohmann_json, std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." + std::to_string(NLOHMANN_JSON_VERSION_MINOR));

#ifdef GEOSTUDIO_NATIVE_HAS_GDAL
#include <gdal.h>
#include <cpl_conv.h>
GEOSTUDIO_NATIVE_ABOUT(gdal, GDAL_RELEASE_NAME)
#endif

#ifdef GEOSTUDIO_NATIVE_HAS_TINYXML
#include <tinyxml.h>
GEOSTUDIO_NATIVE_ABOUT(tinyxml, std::to_string(TIXML_MAJOR_VERSION) + "." + std::to_string(TIXML_MINOR_VERSION))
#endif

#ifdef GEOSTUDIO_NATIVE_HAS_GEOCODER
#include <ogr_geocoding.h>
#endif


using namespace geostudio::native;
using namespace geostudio::native::detail;

// Static object factory map.
// Normally this would go in static.cpp, but since the registration macro (GEOSTUDIO_NATIVE_ADD_OBJECT_FACTORY)
// runs at static initialization time itself, we need to construct the factories map on demand.
std::unordered_map<std::string, ContextImpl::ObjectFactory>&
ContextImpl::objectFactories()
{
    static std::unordered_map<std::string, ContextImpl::ObjectFactory> factories;
    return factories;
}

// static object creation function:
std::shared_ptr<Object>
ContextImpl::createObjectImpl(const std::string& name, const std::string& JSON, const IOOptions& io)
{
    auto i = objectFactories().find(toLower(name));
    if (i != objectFactories().end())
        return i->second(JSON, io);
    return nullptr;
}

#ifdef GEOSTUDIO_NATIVE_HAS_GDAL
namespace
{
    void CPL_STDCALL myCPLErrorHandler(CPLErr errClass, int errNum, const char* msg)
    {
        Log()->info("GDAL says: " + std::string(msg) + std::string(" (error ") + std::to_string(errNum) + ")");
    }
}
#endif

std::set<std::string>&
ContextImpl::about()
{
    static std::set<std::string> about;
    return about;
}

ContextImpl::ContextImpl()
{
#ifdef GEOSTUDIO_NATIVE_HAS_GDAL

    OGRRegisterAll();
    GDALAllRegister();

#if defined(_WIN32) && !defined(GEOSTUDIO_NATIVE_USE_UTF8_FILENAMES_ON_WINDOWS)
    // GDAL assumes filenames are UTF-8 encoded by default.
    // Setting this options to NO tells GDAL to pass filenames through without recoding them.
    CPLSetConfigOption("GDAL_FILENAME_IS_UTF8", "NO");
#endif

  #if GDAL_VERSION_MAJOR>=3
    CPLSetConfigOption("OGR_CT_FORCE_TRADITIONAL_GIS_ORDER", "YES");
  #endif

    // Redirect GDAL/OGR console errors to our own handler
    CPLPushErrorHandler(myCPLErrorHandler);

    // Set the GDAL shared block cache size. This defaults to 5% of
    // available memory which is too high.
    GDALSetCacheMax(40 * 1024 * 1024);

#endif // GEOSTUDIO_NATIVE_HAS_GDAL

    // Tell the weejobs library how to set a thread name
    io.services().jobs.set_thread_name_function([](const char* value) {
        setThreadName(value); });
}

ContextImpl::~ContextImpl()
{
    io.services().jobs.shutdown();
}

UID geostudio::native::createUID()
{
    static std::atomic<unsigned> uidgen = 0;
    return uidgen++;
}

std::string geostudio::native::json_pretty(std::string_view j)
{
    return parse_json(j).dump(4);
}
