#include <geostudio/native/runtime/engine/TilesetAdapter.h>
#include <geostudio/native/runtime/engine/ResourceMaintenance.h>
#include <geostudio/native/runtime/engine/rocky.h>
#include <geostudio/native/render/vsg/GeoTransform.h>
#include <geostudio/native/render/import/all/all.h>
#include <geostudio/native/render/import/osgb/OSGB.h>
#include <geostudio/native/render/import/osgb/OSGBPaging.h>
#include <geostudio/native/render/vsg/ShaderDefines.h>
#include <vsg/io/ObjectFactory.h>
#include <glm/vec4.hpp>
#include <iostream>
#include <string>

#ifdef GLM_FORCE_XYZW_ONLY
#error Cesium private GLM configuration must not escape the SDK
#endif

#ifdef GEOSTUDIO_NATIVE_HAS_IMGUI
#error ImGui configuration must belong to the host adapter
#endif

// Exercise installed module symbols, type registration and private dependency isolation.
int main()
{
    const glm::vec4 color(1.0f);
    auto map = geostudio::native::Map::create();
    auto readers = geostudio::native::io::all::create();
    auto osgb = geostudio::native::io::OSGB::create();
    const bool osgbType = std::string(osgb->className()) == "geostudio::native::io::OSGB";
    // 核验新增诊断接口的安装符号，不创建窗口或读取外部数据。
    const auto osgbStatus = osgb->status();
    const bool osgbDiagnostics = osgbStatus.readRequests == 0 && osgbStatus.attachedPages == 0;
    // 验证新增安装头与链接符号；不启动工作线程、窗口或 GPU。
    auto pager = geostudio::native::io::OSGBDatabasePager::create();
    const auto residency = pager->residencyMetrics();
    const auto ownership = pager->ownershipMetrics();
    const auto maintenance = geostudio::native::resourceMaintenanceMetrics(nullptr);
    const bool resourceDiagnostics = residency.residentPages == 0 && ownership.containerPages == 0 &&
        ownership.closedTokenPages == 0 && maintenance.maintenancePasses == 0;
    auto transform = geostudio::native::GeoTransform::create();
    auto volatile createWindow = &geostudio::native::tiles3d::createTilesetWindow;
    auto factory = vsg::ObjectFactory::instance();
    auto currentReader = factory->create("geostudio::native::io::all");
    auto legacyReader = factory->create("vsgXchange::all");
    const bool types = std::string(readers->className()) == "geostudio::native::io::all"
        && std::string(transform->className()) == "geostudio::native::GeoTransform";
    std::cout << "GeoStudioNative package consumer: " << map->to_json() << std::endl;
    return readers && currentReader && legacyReader && createWindow && color.r == 1.0f && types && osgbType && osgbDiagnostics && resourceDiagnostics ? 0 : 1;
}
