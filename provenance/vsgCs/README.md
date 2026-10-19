# Tiles3D 来源与补丁

当前 namespace 为 geostudio::native::tiles3d，源码位于 src/tiles3d。固定 vsgCs 上游、原路径映射与完整补丁以[顶层 manifest](../manifest.json)为准；本目录 [manifest.json](manifest.json)供 Demo 打包同一份当前完整 patch。

004-modules.patch 包含 namespace、include、构建拆分和此前保留 loadErrorCallback 的差异。旧 001／002 patches 保留历史证据，不与 004 重复应用。Cesium Native Parser／Tile Selection 保持外部固定来源。官方程序源码保留在 examples/tiles3d，未集成进 Demo。

核验入口为 SDK scripts/verify-sources.ps1；迁移、构建身份和许可证规则见[来源总说明](../README.md)。
