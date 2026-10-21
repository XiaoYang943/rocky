# Data

子目录：vector、raster、elevation。

Feature/Geometry、Image/GeoImage、Heightfield 的内存模型与纯计算。这里不建立 Viewer，也不选择输入文件格式。

依赖边界：依赖 Core；不依赖 Scene、Drivers 或 Render。

验证：字段精度、孔洞、插值、NoData、编码；测试位于 test_core_data.cpp 与现有图像/矢量测试。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
