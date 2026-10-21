/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/scene/entity/Registry.h>
#include <geostudio/native/scene/entity/Component.h>
#include <atomic>

std::uint64_t
geostudio::native::detail::nextComponentRevision()
{
    static std::atomic_uint64_t next = 1u;
    return next.fetch_add(1u, std::memory_order_relaxed);
}


