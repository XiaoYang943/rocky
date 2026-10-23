# GeoStudio Native SDK 维护入口

执行工作区 AGENTS 和 [公共规约](../../docs/开发规范/README.md)。repoId 为 geostudio-native，独立 Git 根；当前能力、分层与限制见 [SDK 架构](docs/design/SDK架构与迁移.md)，构建和消费见 [使用说明](docs/design/安装构建与使用.md)。

## 源码与构建

- 源码按 src/core、data、scene、drivers、streaming、render、runtime 分类，头源同目录。公开 header 由 cmake/capability-headers.json 显式导出；源码和生成视图不能同时手改。
- 构建入口 scripts/build.ps1，安装到 out/install-capabilities。只用锁定外部依赖，不恢复上游嵌套 project 或任意升级消费版本。
- 公共 API 保持 C++17，私有 Cesium adapter 使用 C++20；GLM 编译定义、Cesium 类型与 UI 依赖不能泄漏给宿主。
- SDK 不依赖 ImGui；需要它的宿主自行管理适配；当前 Tauri Demo 不编译 native/ui。新增 UI 功能不得回到 src。
- namespace／公开对象布局变化需更新 SDK ABI／版本并重建 consumer。完整来源 patch 与映射遵守 GEOSTUDIO.md 和 provenance。

## 修改与验证

- 每轮先核对用户授权，只修改当前范围，不格式化其他代码；新增注释使用中文，底层术语遵循工作区约定。存量英文注释专项另行实施，原版权和许可证保留。
- 新函数说明用途、ownership、threading 和失败行为。输入原件与依赖 Cache 只读，用户配置和 Git 历史保持。
- 先建立相关代码回归，再做实现迁移；小型 fixture 应明确输入与期望，不能用宽松判定掩盖失败。架构检查和独立 package consumer 为模块迁移关卡。
- 本轮仅做 CPU 代码测试和 Demo 编译，不启动软件、窗口或 GPU 测试；人工验收由用户完成。
- 文件删除按工作区限制逐个明确路径；不批量删除，不清空失败证据或旧制品。
- 设计维护于 docs/design 的既有正文；过程和自检在对话交付，不新建任务流水。提交、推送、发布分别授权。
