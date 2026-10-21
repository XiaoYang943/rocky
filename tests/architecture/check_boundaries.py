"""只读检查能力边界、公开头文件和安装契约；不依赖窗口或 GPU。"""
import argparse
import json
from pathlib import Path
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--installed", type=Path)
arguments = parser.parse_args()
ALLOWED = {
    "core": set(),
    "data": {"core"},
    "scene": {"core", "data"},
    "drivers": {"core", "data", "scene"},
    "streaming": {"core", "data", "scene"},
    "render": {"core", "data", "scene", "drivers", "streaming"},
    "runtime": {"core", "data", "scene", "drivers", "streaming", "render"},
}
GENERATED = {
    "geostudio/native/core/base/Version.h": True,
    "geostudio/native/render/import/all/Version.h": True,
    "geostudio/native/runtime/engine/Export.h": True,
    "geostudio/native/render/shaders/rocky.defines.h.glsl": True,
    "geostudio/native/core/base/cesium/Export.h": False,
    "geostudio/native/render/tileset/cesium/Config.h": False,
}
errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


headers = json.loads((ROOT / "cmake/capability-headers.json").read_text(encoding="utf-8"))
by_include = {h["include"]: h for h in headers}
require(len(by_include) == len(headers), "重复的 header 导出路径")
visibility = {h["include"]: h["public"] for h in headers}
visibility.update(GENERATED)
for header in headers:
    path = ROOT / header["path"]
    require(path.is_file(), f"缺少 header 源文件：{header['path']}")
    require(not header["public"] or header["path"].startswith("src/"), f"宿主适配不得安装：{path}")
    if not path.is_file():
        continue
    text = path.read_text(encoding="utf-8-sig")
    for include in re.findall(r'#include\s*[<"](geostudio/[^">]+)', text):
        require(include in visibility, f"未知 header：{path} -> {include}")
        if header["public"]:
            require(visibility.get(include, False), f"私有 header 泄漏：{path} -> {include}")

for path in (ROOT / "src").rglob("*"):
    if path.suffix not in {".h", ".cpp", ".in", ".glsl"}:
        continue
    relative = path.relative_to(ROOT).as_posix()
    capability = path.relative_to(ROOT / "src").parts[0]
    require(capability in ALLOWED, f"未知源码能力：{relative}")
    text = path.read_text(encoding="utf-8-sig")
    require("ROCKY" + "_" not in text, f"旧宏：{relative}")
    require(not re.search(r"ImGui|imgui|geostudio/host", text), f"SDK 引入宿主 UI：{relative}")
    for include in re.findall(r'#include\s*[<"](geostudio/native/[^">]+)', text):
        require(include in visibility, f"未知源码 include：{relative} -> {include}")
        dependency = include.split("/")[2]
        require(dependency == capability or dependency in ALLOWED.get(capability, set()),
                f"逆向依赖：{relative} -> {include}")

for scope in ["resources", "examples", "tests"]:
    for path in (ROOT / scope).rglob("*"):
        if path.suffix not in {".h", ".cpp", ".glsl", ".cmake", ".in"}:
            continue
        text = path.read_text(encoding="utf-8-sig")
        require("ROCKY" + "_" not in text, f"旧宏：{path.relative_to(ROOT)}")

cmake = (ROOT / "cmake/GeoStudioNativeConfig.cmake.in").read_text(encoding="utf-8")
require("imgui" not in cmake.lower(), "安装包要求 ImGui")
require("Cesium" not in cmake, "安装包泄漏 Cesium 私有依赖")
assembly = (ROOT / "cmake/GeoStudioNative.cmake").read_text(encoding="utf-8")
require("examples/imgui-adapter" not in assembly, "SDK 构建仍引用宿主 UI")
require("install(DIRECTORY provenance/" not in assembly, "安装包包含历史来源目录")
require((ROOT / "tests/package-consumer/CMakeLists.txt").is_file(), "缺少独立安装包消费测试")
require(all(h["path"].startswith("src/") for h in headers), "SDK header 清单包含宿主文件")
require(not (ROOT / "examples").exists(), "历史 examples 目录重新进入 SDK")
require(not (ROOT / "tools").exists(), "历史 tools 目录重新进入 SDK")
require('install(DIRECTORY "${GEOSTUDIO_PUBLIC_INCLUDE}' not in assembly,
        "禁止整目录安装缓存 header，避免过期公开接口重新进入安装包")
sources = (ROOT / "cmake/capability-sources.cmake").read_text(encoding="utf-8")
listed = re.findall(r'\$\{PROJECT_SOURCE_DIR\}/([^"\n]+)', sources)
require(len(listed) == len(set(listed)), "能力库重复编译相同源文件")
for relative in listed:
    require((ROOT / relative).is_file(), f"构建引用不存在：{relative}")
require("slug/SlugAdapter.cpp" not in sources, "可选 Slughorn 混入默认 C++17 能力库")
require(not any("/cesium/" in value for value in listed), "Cesium 源码混入公共能力库")

if arguments.installed:
    include_root = arguments.installed / "include"
    for header in headers:
        installed = include_root / header["include"]
        if header["public"]:
            require(installed.is_file(), f"安装包缺少：{header['include']}")
            if installed.is_file():
                require(installed.read_bytes() == (ROOT / header["path"]).read_bytes(),
                        f"安装 header 与源码不同：{header['include']}")
        else:
            require(not installed.exists(), f"安装包暴露私有 header：{header['include']}")
    expected = {name for name, public in visibility.items() if public}
    for path in (include_root / "geostudio").rglob("*"):
        if path.is_file():
            require(path.relative_to(include_root).as_posix() in expected, f"过期安装 header：{path}")
    for name, public in GENERATED.items():
        require((include_root / name).is_file() == public, f"生成 header 的安装范围不符：{name}")

for error in errors:
    print(error)
if errors:
    sys.exit(1)
print(f"边界检查通过：{len(ALLOWED)} 个能力、{len(headers)} 个 header、{len(listed)} 个公共能力源文件。")
