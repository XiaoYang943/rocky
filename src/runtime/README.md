# Runtime

子目录：engine。

Application 装配、帧循环和跨系统生命周期；TilesetAdapter 为宿主暴露受控的 3D Tiles 入口。

依赖边界：Runtime 链接 Render 和 RuntimeTiles DLL；RuntimeTiles 私有链接 Cesium adapter。运行中关闭仍遵循 close→pump→retired→释放→shutdown。

验证：独立 package consumer 检查公共 ABI、类型注册与依赖隔离；实际 Viewer/GPU 生命周期测试本轮未运行。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
