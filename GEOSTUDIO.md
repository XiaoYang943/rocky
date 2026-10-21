# GeoStudio Native 来源与维护约定

本仓库保留 XiaoYang943/rocky 完整 Git 历史和原 origin；固定上游基线仍为 a4d8b141ec478e8440526928b344c6eb93810631。当前工作树任务为 [NativeSDK-004](docs/design/任务/004-宿主适配迁移与发行内容整理.md)，实现起点 HEAD 见主任务，不能用固定上游 commit 冒充当前实现版本。

源码按 src 下七个能力组织；头源同目录。原来源对应关系见 [capability-migration.json](provenance/capability-migration.json)，上一轮对应关系保留于 module-migration.json。VSG／Cesium Native／GDAL 等外部依赖不升级，版权和许可证不改。

[当前来源清单](provenance/manifest.json)登记固定归档 hash、原路径和 hash、当前路径和 hash、完整 patch。005-capabilities.patch 记录完整文本差异，采用最终路径，可对当前源码 reverse check；已删除的二进制样例由 manifest 中的 binaryDeletions 记录原路径、原 SHA-256、原大小和删除动作，原 bytes 可由固定上游 ZIP 恢复。旧 patches 和 manifest 保留历史，不参与当前补丁链。自有实现和构建脚本由 installed build-manifest.json 记录。

新增文件、迁移或源码变更后，先解释授权范围与差异，再运行 scripts/refresh-provenance.py 导出完整 patch，最后 scripts/verify-sources.ps1 核验。禁止只修改 hash 绕过校验。原件 ZIP 从 dependencies Cache 读取，校验后只读使用。

源码和公共头文件由 CMake 显式清单编排；真实 namespace 为 geostudio::native 及既有 io／tiles3d 子空间。自有宏使用 GEOSTUDIO_NATIVE_*，外部第三方宏和资源文件名维持原契约。SDK 无 ImGui 依赖，UI 实现位于 Demo 的 native/ui，其来源由 Demo 独立核验。

历史技术文档的路径和符号保留当时含义，通过迁移 map 查询当前实现；不自动重写全部历史材料。用户 .obsidian 配置、既有制品与 Git 历史不动。所有删除、提交、推送继续遵守工作区授权规则。
