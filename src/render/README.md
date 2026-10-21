# Render

子目录：entity、terrain、view、vsg、paging、import、model、resources、geospatial、tileset。

把 Scene/Data 转成 VSG 可渲染对象。DisplayManager/MapManipulator/TerrainPicking 同属视图操作。Import 是 VSG Object 的格式转换，区别于 GIS Drivers。

依赖边界：公共 Render 依赖 Streaming/Drivers/Scene 与 Import。各 cesium 子目录由私有 adapter 构建；Overlay 绑定依旧是渲染后端内部接口。

验证：现有 Tessellation、Overlay、Texture、TerrainAnchor 等 CPU 回归，以及 OBJ/PNG fixture；没有 Device/Window/GPU 验收。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
