# Streaming

子目录：terrain。

TerrainTileModel/Factory、ElevationSampler，组织数据请求、采样与组合。VSG Node 分页仍在 Render/paging，Cesium 选择策略仍由外部依赖负责。

依赖边界：依赖 Scene/Data/Core；不能反向引用 Render 或 Application。

验证：现有高程和空间测试；私有 AssetAccessor/JSON 合约由 code-streaming 测试集覆盖。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
