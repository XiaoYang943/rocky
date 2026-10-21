# Core

子目录：base、geo（含 tiling）、math、async、cache、io。

空间参考、基础值类型、资源协议与任务工具。TileKey/Profile 属于空间划分，不能反向依赖 Data。

依赖边界：不依赖其他 SDK 能力。Context 的 GDAL/CURL/PROJ 集成沿用现有实现；core/*/cesium 为私有后端代码，由独立 adapter target 编译。

验证：SRS、GeoExtent、TileKey、URI、Callback、Cache；测试位于 tests/globe 与 tests/capabilities/test_core_data.cpp。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
