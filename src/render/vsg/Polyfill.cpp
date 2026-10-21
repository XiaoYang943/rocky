/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/render/vsg/Polyfill.h>

using namespace geostudio::native;

VulkanExtensions::VulkanExtensions(vsg::Device* device)
{
    GEOSTUDIO_NATIVE_HARD_ASSERT(device);

    device->getProcAddr<PFN_vkCmdSetPolygonModeEXT>(vkCmdSetPolygonMode, "vkCmdSetPolygonModeEXT");
    device->getProcAddr<PFN_vkCmdSetDepthWriteEnableEXT>(vkCmdSetDepthWriteEnable, "vkCmdSetDepthWriteEnableEXT");
    device->getProcAddr<PFN_vkCmdSetCullModeEXT>(vkCmdSetCullMode, "vkCmdSetCullModeEXT");
    device->getProcAddr<PFN_vkCmdSetColorWriteMaskEXT>(vkCmdSetColorWriteMask, "vkCmdSetColorWriteMaskEXT");
}
