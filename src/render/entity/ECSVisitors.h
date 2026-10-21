/**
 * rocky c++
 * Copyright 2025 Pelican Mapping
 * MIT License
 */
#pragma once
#include <geostudio/native/scene/entity/ECS.h>
#include <geostudio/native/render/vsg/VSGContext.h>

namespace geostudio::native
{
    /**
    * Base class for ECS visitors.
    */
    class GEOSTUDIO_NATIVE_API ECSVisitor
    {
    public:
        std::uint32_t viewID = 0;
        entt::entity currentEntity = entt::null;
        std::unordered_set<entt::entity> collectedEntities;

        inline void reset() {
            collectedEntities.clear();
            currentEntity = entt::null;
        }

    protected:
        inline ECSVisitor(std::uint32_t in_viewID) :
            viewID(in_viewID) {
        }
    };

    namespace detail
    {
        /**
        * Specializes the VSG polytope intersector to locate entity components.
        */
        class GEOSTUDIO_NATIVE_API ECSPolytopeIntersector : public vsg::Inherit<vsg::PolytopeIntersector, ECSPolytopeIntersector>,
            public ECSVisitor
        {
        public:
            //! Creates a window-space polytope query; requires a view with a valid camera and its matrices.
            ECSPolytopeIntersector(vsg::View* view, double xMin, double yMin, double xMax, double yMax,
                vsg::ref_ptr<vsg::ArrayState> initialArrayData = {}) :
                Inherit(*view->camera, xMin, yMin, xMax, yMax, initialArrayData),
                ECSVisitor(view->viewID)
            {
                //nop
            }

            //! Collects the current entity when a non-indexed draw intersects the query.
            bool intersectDraw(
                uint32_t firstVertex, uint32_t vertexCount, uint32_t firstInstance, uint32_t instanceCount) override
            {
                bool intersects = Inherit::intersectDraw(firstVertex, vertexCount, firstInstance, instanceCount);
                if (intersects && currentEntity != entt::null)
                    collectedEntities.emplace(currentEntity);
                return intersects;
            }

            //! Collects the current entity when an indexed draw intersects the query.
            bool intersectDrawIndexed(
                uint32_t firstIndex, uint32_t indexCount, uint32_t firstInstance, uint32_t instanceCount) override
            {
                bool intersects = Inherit::intersectDrawIndexed(firstIndex, indexCount, firstInstance, instanceCount);
                if (intersects && currentEntity != entt::null)
                    collectedEntities.emplace(currentEntity);
                return intersects;
            }
        };
    }

    /**
    * Specializes the VSG line segment intersector to locate entity components.
    */
    class GEOSTUDIO_NATIVE_API ECSLineSegmentIntersector : public vsg::Inherit<vsg::LineSegmentIntersector, ECSLineSegmentIntersector>,
        public ECSVisitor
    {
    public:
        ECSLineSegmentIntersector(vsg::View* view, int32_t x, int32_t y, vsg::ref_ptr<vsg::ArrayState> initialArrayData = {}) :
            Inherit(*view->camera, x, y, initialArrayData),
            ECSVisitor(view->viewID)
        {
            //nop
        }

        bool intersectDraw(uint32_t firstVertex, uint32_t vertexCount, uint32_t firstInstance, uint32_t instanceCount) override
        {
            bool intersects = Inherit::intersectDraw(firstVertex, vertexCount, firstInstance, instanceCount);
            if (intersects && currentEntity != entt::null)
                collectedEntities.emplace(currentEntity);
            return intersects;
        }

        bool intersectDrawIndexed(uint32_t firstIndex, uint32_t indexCount, uint32_t firstInstance, uint32_t instanceCount) override
        {
            bool intersects = Inherit::intersectDrawIndexed(firstIndex, indexCount, firstInstance, instanceCount);
            if (intersects && currentEntity != entt::null)
                collectedEntities.emplace(currentEntity);
            return intersects;
        }
    };
}

EVSG_type_name(geostudio::native::detail::ECSPolytopeIntersector)
EVSG_type_name(geostudio::native::ECSLineSegmentIntersector)
