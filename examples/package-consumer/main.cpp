#include <geostudio/native/tiles3d/TilesetAdapter.h>
#include <geostudio/native/globe/rocky.h>
#include <geostudio/native/globe/vsg/GeoTransform.h>
#include <geostudio/native/io/all.h>
#include <vsg/io/ObjectFactory.h>
#include <glm/vec4.hpp>
#include <iostream>
#include <string>

#ifdef GLM_FORCE_XYZW_ONLY
#error Cesium private GLM configuration must not escape the SDK
#endif

// Exercise installed module symbols, type registration and private dependency isolation.
int main()
{
    const glm::vec4 color(1.0f);
    auto map = geostudio::native::Map::create();
    auto readers = geostudio::native::io::all::create();
    auto transform = geostudio::native::GeoTransform::create();
    auto volatile createWindow = &geostudio::native::tiles3d::createTilesetWindow;
    auto factory = vsg::ObjectFactory::instance();
    auto currentReader = factory->create("geostudio::native::io::all");
    auto legacyReader = factory->create("vsgXchange::all");
    const bool types = std::string(readers->className()) == "geostudio::native::io::all"
        && std::string(transform->className()) == "geostudio::native::GeoTransform";
    std::cout << "GeoStudioNative package consumer: " << map->to_json() << std::endl;
    return readers && currentReader && legacyReader && createWindow && color.r == 1.0f && types ? 0 : 1;
}
