# 能力模块

本目录按长期职责划分，来源库通过 provenance 追溯。参考 VSG 将节点、遍历、I/O、运行装配分开，以及 osgEarth 将空间模型、数据 Drivers 与应用代码分开的方式；没有复制三个上游仓库的目录作为顶层组件。

依赖方向为 Runtime → Render → Streaming／Drivers → Scene → Data → Core。Render 内的 Import 直接依赖 VSG；私有 Cesium 后端源码按职责放在对应子目录，由独立 C++20 target 装配。详见[架构说明](../docs/design/SDK架构与迁移.md)。

每个模块的 README 列出责任和依赖；新增接口先决定其归属、公开性、ownership 和测试位置，再修改显式 source/header 清单。禁止低层反向引用 Runtime，也不把 UI 控件放入 SDK。跨模块不能仅靠相对路径绕过依赖约束。
