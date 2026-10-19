# GeoStudio Native SDK 维护入口

执行工作区 AGENTS 和 [公共规约](../../docs/开发规范/README.md)。repoId 为 geostudio-native，独立 Git 根；当前任务为 [NativeSDK-002](docs/design/任务/002-Namespace与模块重组.md)。旧整合事实见 NativeSDK-001，不代替本轮验收。

## 源码与构建

- 公共 header：include/geostudio/native；源码模块：src/globe、src/io、src/tiles3d；资源：resources。外部依赖 namespace 和版本保持。
- 构建只使用 scripts/build.ps1，安装到 out/install-modules。按 README／安装指南准备环境，不恢复上游独立 bootstrap 或嵌套 project。
- Globe／公开 consumer 保持 C++17，Tiles3D runtime 使用 C++20 并隔离 private dependencies。不能让 GLM 编译定义或第二个 ImGui context 泄漏到宿主。
- namespace／公开对象布局变化需更新 SDK ABI／版本，重新构建 consumer。来源原件、完整 patches 和迁移映射遵守 GEOSTUDIO.md 与 provenance。

## 修改与验证

- 每轮代码修改核对用户授权，只修改当前范围，不格式化其他代码；沿用文件换行，新增 C++ 使用 4 空格、英文注释与 128 字符行宽。
- 新函数说明用途、ownership、threading 和失败行为。用户文档、配置、Git 历史与未授权修改保持。
- 测试仅针对代码工具和基础功能；使用既有回归、独立 package consumer 和必要 Demo 联合检查，人工验收由用户完成。
- 实际输入数据和外部 dependency cache 只读。清理按工作区约束及用户当轮授权，先登记明确路径、用途和引用。
- 本轮不集成新 Demo 功能。原示例源码保留于 examples，详见索引，不能将源文件存在写成已验证。
- 设计文档放 docs/design；新增文档交付时说明尚未提交 Git。提交、推送与发布需要各自授权。
