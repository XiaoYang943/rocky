/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */

// A place to initialize statics that require proper ordering
#include <geostudio/native/core/base/Common.h>
#include <geostudio/native/core/async/weejobs.h>

// job system definition
#ifdef WEEJOBS_USE_SINGLETON
WEEJOBS_INSTANCE;
#endif
