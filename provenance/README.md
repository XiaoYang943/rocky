# 来源、映射与完整 patches

[manifest.json](manifest.json)为当前 GeoStudioNative 0.2.0 来源权威。外部依赖见 [dependencies.lock.json](../dependencies.lock.json)，不自动升级。

| 来源 | 固定 commit | 当前模块 |
| --- | --- | --- |
| Rocky | a4d8b141ec478e8440526928b344c6eb93810631 | src/globe、include/geostudio/native/globe |
| vsgXchange | bdc241ad1d6813ebc2a1777d35885a39e143297b | src/io、include/geostudio/native/io |
| vsgCs | 72176fdfb35acb8462c5760c1b432d72f19c767a | src/tiles3d，runtime headers 不随公开 SDK 安装 |

每个来源的 module-files.json 覆盖固定 ZIP 的文件：upstreamPath／upstreamSha256 对应原件，path／sha256 对应当前消费；path 为 null 表示清理。004-modules.patch 是从固定上游原件到当前内容的完整文本差异，采用最终路径，移动关系见 [module-migration.json](module-migration.json)。二进制资源保持原 bytes。旧 001／002／003 patches 与 consumed-files.json 只记录前一轮，不再组成当前补丁链。

迁移 map 记录本轮删除理由、原路径、原 hash 和移除的空目录。文件删除逐个核对，Git 历史、许可证、用户资料及旧制品保留。Rocky 上游文档中本轮之前已有的用户差异仍原样保留；完整原件对照可能包含这些既有差异，不将它们归为本轮编写。

`./scripts/verify-sources.ps1` 核验当前文件身份、映射 hash、patch hash 与 reverse apply check。新的自有代码和构建装配用 installed build-manifest.json 记录。新增／删除／改名后须复核完整清单与源码差异，不能只更新 hash。

后续导出应从 manifest 中固定 archive URL／SHA-256 获取原件，按 module-files 和迁移 map 对照内容；只更新已批准变更。旧版本补丁保留历史事实。vsgCs 可选 Tracy submodule 不在原 ZIP 中，本 SDK 不启用它，不声称接管其源码。

许可证保留在根 LICENSE.txt、各来源 upstream/LICENSE.md，并安装到 share/GeoStudioNative/licenses。运行资源集中到 resources；研究原文保留在 upstream 文档目录，原文链接可能使用旧仓库相对路径，实际源码请按迁移 map 定位。
