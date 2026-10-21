/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once

#include <geostudio/native/render/vsg/Common.h>
#include <geostudio/native/render/entity/System.h>
#include <geostudio/native/scene/entity/Registry.h>

namespace geostudio::native
{
    /**
    * System that analyzes Declutter components and adjusts entity Visibility
    * components accordingly.
    */
    class GEOSTUDIO_NATIVE_API DeclutterSystem : public System
    {
    public:
        enum class Sorting
        {
            Priority = 0, // sort by priority
            Distance = 1, // sort by distance to camera
        };

        //! Buffer in pixels around decluttered entities
        float bufferPixels = 0.0f;

        //! Method to use when prioritizing entities that overlap
        Sorting sorting = Sorting::Priority;

    public:
        //! Create constructor
        static std::shared_ptr<DeclutterSystem> create(Registry registry) {
            return std::make_shared<DeclutterSystem>(registry);
        }

        //! Construct the system (use create)
        DeclutterSystem(Registry r);

        //! Call periodically to update the visibility state of entities
        void update(VSGContext runtime) override;

        //! Gets two stats, the number of entities marked visible in the last update,
        //! and the total number of entities considered in the last update.
        std::pair<unsigned, unsigned> visibleAndTotal() const;

    protected:

        bool _enabled = true;
        unsigned _visible = 1;
        unsigned _total = 0;
        std::size_t _last_max_size = 32;
    };
}
