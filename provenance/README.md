# 来源、映射与完整 patches

[manifest.json](manifest.json)为当前 GeoStudioNative 0.3.0 来源权威。外部依赖见 [dependencies.lock.json](../dependencies.lock.json)，不自动升级。

| 来源 | 固定 commit | 当前模块 |
| --- | --- | --- |
| Rocky | a4d8b141ec478e8440526928b344c6eb93810631 | core／data／scene／drivers／streaming／render／runtime；UI adapter 已转交 Demo |
| vsgXchange | bdc241ad1d6813ebc2a1777d35885a39e143297b | src/render/import |
| vsgCs | 72176fdfb35acb8462c5760c1b432d72f19c767a | 按能力分组的 cesium 子目录；私有 headers 不安装 |

每个来源的 module-files.json 覆盖固定 ZIP 的文件：upstreamPath／upstreamSha256 对应原件，path／sha256 对应当前消费；path 为 null 表示清理。005-capabilities.patch 是从固定上游原件到当前内容的完整文本差异，采用最终路径，本轮移动关系见 [capability-migration.json](capability-migration.json)，上一轮记录见 [module-migration.json](module-migration.json)。保留的 runtime 二进制资源保持原 bytes；已删除二进制样例使用 manifest 的 binaryDeletions 登记。旧 001／002／003／004 patches 与 consumed-files.json 只记录前一轮，不再组成当前补丁链。

迁移 map 记录本轮删除理由、原路径、原 hash 和移除的空目录。文件删除逐个核对，Git 历史、许可证、用户资料及旧制品保留。Rocky 上游文档中本轮之前已有的用户差异仍原样保留；完整原件对照可能包含这些既有差异，不将它们归为本轮编写。

`./scripts/verify-sources.ps1` 核验当前文件身份、映射 hash、patch hash 与 reverse apply check。新的自有代码和构建装配用 installed build-manifest.json 记录。新增／删除／改名后须复核完整清单与源码差异，不能只更新 hash。

后续导出应从 manifest 中固定 archive URL／SHA-256 获取原件，按 module-files 和迁移 map 对照内容；只更新已批准变更。旧版本补丁保留历史事实。vsgCs 可选 Tracy submodule 不在原 ZIP 中，本 SDK 不启用它，不声称接管其源码。

许可证保留在根 LICENSE.txt、各来源 upstream/LICENSE.md，并安装到 share/GeoStudioNative/licenses。运行资源集中到 resources；研究原文保留在 upstream 文档目录，原文链接可能使用旧仓库相对路径，实际源码请按迁移 map 定位。

当前重建入口 `scripts/refresh-provenance.py` 会校验固定 ZIP，再生成完整差异和内容清单。历史构建入口保留在 build-history，不再被活动 CMake 引用。第 1 轮没有执行历史文件删除；第 2 轮按用户明确授权，核对 110 个文件后逐文件执行删除，再移除空目录。

NativeSDK-004 将 UI 转交 Demo native/ui，路径为 null 且含 transferredTo 的记录表示跨仓库迁移，不能解释为代码丢弃。具体映射见 [host-migration.json](host-migration.json)。Demo 保存自己的固定来源、完整 patch 和许可证，独立执行 ui-provenance.py。

新安装包只包含 manifest.json、各来源 module-files.json、manifest 指向的活动完整文本 patch，以及单独安装的许可证。manifest schemaVersion=3 另含 binaryDeletions：2 个 TIFF 和 1 个 GLB 的删除动作及原件身份。refresh-provenance.py 对固定 ZIP 校验 bytes 和 SHA-256 后生成这些记录；verify-sources.ps1 检查记录与来源映射一致、原消费路径确实不存在。二进制修改仍要求显式 binary patch，不能借删除记录跳过修改检查。旧 patch、build-history、上游研究文档和迁移历史保留在源码仓库，不复制到新安装包。现有安装目录增量安装不会自动删除旧文件；严格发行检查应对全新目录执行。历史 examples／tools 已完成清理；109 个上游映射登记 removedPath，1 个自有索引从 ownedFiles 移除，sourceScopes 不再包含这两个目录。
