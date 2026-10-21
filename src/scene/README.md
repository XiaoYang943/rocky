# Scene

子目录：map、layer、entity、view。

可见对象的状态、图层协议与 ECS 组件；Viewpoint 是相机目标数据，实际窗口与相机操作属于 Render。

依赖边界：依赖 Data/Core；Entity 使用 EnTT 和现有 VSG 类型，不反向包含 Runtime。

验证：Map snapshot、事件生命周期、Layer 错误状态、Terrain-RGB 解码；tests/capabilities/test_layer_contracts.cpp。

头文件与实现同目录；公开项以 [header 清单](../../cmake/capability-headers.json) 为准。新增源文件同步 source 清单和模块测试，禁止低层反向依赖高层。来源路径通过 [迁移映射](../../provenance/capability-migration.json) 查询。
