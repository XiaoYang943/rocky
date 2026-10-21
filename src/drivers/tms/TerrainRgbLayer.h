#pragma once
#include <geostudio/native/scene/layer/ElevationLayer.h>
#include <geostudio/native/drivers/tms/TMS.h>

namespace geostudio::native
{
    /**
     * 读取本地或 HTTP(S) Terrain-RGB PNG，复用 ElevationLayer 的 MapboxRGB 解码。
     * 配置须在 open 前完成；Layer 自有 TMS Driver，关闭时释放 Driver。
     * 不追加服务商查询参数，读取失败由 Result 传回；调用线程遵循 TileLayer 约定。
     */
    class TerrainRgbLayer : public Inherit<ElevationLayer, TerrainRgbLayer>, public TMS::Options
    {
    public:
        TerrainRgbLayer() { encoding = Encoding::MapboxRGB; }

    protected:
        Result<> openImplementation(const IOOptions& io) override
        {
            auto result = ElevationLayer::openImplementation(io);
            if (!result) return result;
            DataExtentList extents;
            result = _driver.open(uri, profile, "png", extents, io);
            if (result) setDataExtents(extents);
            return result;
        }

        void closeImplementation() override
        {
            _driver.close();
            ElevationLayer::closeImplementation();
        }

        Result<GeoImage> createTileImplementation(const TileKey& key, const IOOptions& io) const override
        {
            auto image = _driver.read(key, invertY, false, uri->context(), io);
            if (!image) return image.error();
            return GeoImage(image.value(), key.extent());
        }

    private:
        TMS::Driver _driver;
    };
}
