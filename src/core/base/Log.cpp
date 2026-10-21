/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/core/base/Log.h>
#include <geostudio/native/core/io/Context.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <atomic>
#include <mutex>

GEOSTUDIO_NATIVE_ABOUT(spdlog, std::to_string(SPDLOG_VER_MAJOR) + "." + std::to_string(SPDLOG_VER_MINOR) + "." + std::to_string(SPDLOG_VER_PATCH));

using namespace geostudio::native;
using namespace geostudio::native::detail;

namespace
{
    struct LogShutdownGuard
    {
        std::atomic_bool* shutting_down;

        ~LogShutdownGuard()
        {
            shutting_down->store(true, std::memory_order_release);
        }
    };

    std::atomic_bool& logShuttingDown()
    {
        // memcheck will report this as a leak. That is ok.
        static auto* value = new std::atomic_bool(false);
        return *value;
    }

    Logger nullLogger()
    {
        static Logger* logger = new Logger(std::make_shared<spdlog::logger>("rocky.shutdown"));
        (*logger)->set_level(spdlog::level::off);
        return *logger;
    }
}

Logger geostudio::native::Log()
{
    static std::once_flag s_once;
    static Logger s_logger;
    static LogShutdownGuard s_shutdown_guard{ &logShuttingDown() };

    if (logShuttingDown().load(std::memory_order_acquire))
        return nullLogger();

    std::call_once(s_once, []()
        {
            const spdlog::level::level_enum default_level = spdlog::level::info;
            (void)nullLogger();

            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            s_logger = std::make_shared<spdlog::logger>("rocky", sink);
            s_logger->set_pattern("%^[%n %l]%$ %v");

            auto log_level = getEnvVar("GEOSTUDIO_NATIVE_LOG_LEVEL");
            if (!log_level.has_value()) log_level = getEnvVar("GEOSTUDIO_NATIVE_NOTIFY_LEVEL");
            if (!log_level.has_value()) s_logger->set_level(default_level);
            else if (ciEquals(log_level.value(), "trace")) s_logger->set_level(spdlog::level::trace);
            else if (ciEquals(log_level.value(), "info")) s_logger->set_level(spdlog::level::info);
            else if (ciEquals(log_level.value(), "debug")) s_logger->set_level(spdlog::level::debug);
            else if (ciEquals(log_level.value(), "warn")) s_logger->set_level(spdlog::level::warn);
            else if (ciEquals(log_level.value(), "error")) s_logger->set_level(spdlog::level::err);
            else if (ciEquals(log_level.value(), "critical")) s_logger->set_level(spdlog::level::critical);
            else if (ciEquals(log_level.value(), "off")) s_logger->set_level(spdlog::level::off);
            else s_logger->set_level(default_level);
        });

    return s_logger;
}
