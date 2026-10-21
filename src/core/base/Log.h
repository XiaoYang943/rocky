/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/core/base/Common.h>
#include <spdlog/spdlog.h>

namespace geostudio::native
{
    namespace log = spdlog;

    using Logger = std::shared_ptr<spdlog::logger>;
    extern GEOSTUDIO_NATIVE_API Logger Log();
}
