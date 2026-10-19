# GeoStudio Native 来源与维护约定

本仓库保留 XiaoYang943/rocky 的完整 Git 历史，基线 a4d8b141ec478e8440526928b344c6eb93810631；origin 未改变。当前任务为 [NativeSDK-002](docs/design/任务/002-Namespace与模块重组.md)。

Rocky 源码进入 src/globe 和 include/geostudio/native/globe；vsgXchange 进入 src/io 和 include/geostudio/native/io；vsgCs 进入 src/tiles3d。外部依赖仍按 dependencies.lock.json，未升级 VSG／Cesium Native／GDAL。保留全部来源 copyright、license 和必要资源；品牌与 namespace 改名不改变上游署名。

[来源清单](provenance/manifest.json)采用 schemaVersion 2：每个上游文件登记原路径／hash、当前路径／hash 或明确删除，完整 patch 采用最终部署路径，可以对当前源码 reverse check。目录移动另见 [module-migration.json](provenance/module-migration.json)。旧补丁和旧 manifest 文件保留作为 NativeSDK-001 历史证据，当前来源以顶层 manifest 为准。

变更不得只改 hash：先解释源码差异、导出完整 patch、核验应用及映射，再构建 SDK／consumer／Demo。自有模块 CMake、Adapter 和 scripts 由 build-manifest.json 记录，不能用 HEAD 冒充未提交工作树。

原 docs/design/vector-overlay-picking.md、技术文档和 IDE 配置保持原内容。历史文档中的 rocky 路径与符号保留其当时含义；通过映射查询当前实现。新增模块采用已有上游风格；当前构建入口为 scripts/build.ps1，不再使用已移除的上游 bootstrap／嵌套 project。
