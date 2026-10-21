/**
 * rocky c++
 * Copyright 2026 Pelican Mapping
 * MIT License
 */
#include <geostudio/native/render/vsg/GeoTransform.h>

using namespace geostudio::native;

void
GeoTransform::traverse(vsg::RecordTraversal& record) const
{
    if (revision != _transformDetail.sync.revision)
    {
        _transformDetail.sync = *this;
    }

    _transformDetail.traverse(record, nullptr);

    RenderingState rs{
        record.getCommandBuffer()->viewID,
        record.getFrameStamp()->frameCount
    };

    if (_transformDetail.passingCull(rs))
    {
        _transformDetail.push(record);
        Inherit::traverse(record);
        _transformDetail.pop(record);
    }
}
