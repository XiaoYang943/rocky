#pragma once
#include <geostudio/native/render/import/all/Export.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <cstdint>

namespace geostudio::native::io::detail
{
// 文件系统调用专用绝对路径；Windows 添加扩展长度前缀，不用于 manifest 或逻辑路径比较。
VSGXCHANGE_DECLSPEC std::filesystem::path nativeFilePath(const std::filesystem::path& path);
// 解析真实存在的前缀并统一路径身份；Windows 去除扩展前缀，供包含关系和相对路径计算。
VSGXCHANGE_DECLSPEC std::filesystem::path canonicalFilePath(const std::filesystem::path& path);
// 流式读取文件并返回 SHA-256；不写输入，调用线程独占哈希上下文，失败抛异常。
VSGXCHANGE_DECLSPEC std::string sha256File(const std::filesystem::path& path);
// 对调用者拥有的字节计算 SHA-256，不保留输入；失败抛异常。
VSGXCHANGE_DECLSPEC std::string sha256Bytes(std::string_view bytes);
// 读取有限大小的文件副本；不保留文件句柄，超限或读取失败抛异常。
VSGXCHANGE_DECLSPEC std::string readBoundedFile(const std::filesystem::path& path, uint64_t maximumBytes);
// 解析现存前缀后检查目录包含关系，防止符号链接及 .. 逃逸；不写文件。
VSGXCHANGE_DECLSPEC bool pathWithin(const std::filesystem::path& child, const std::filesystem::path& parent);
// 检查独立制品目录，不创建目录；禁止输入原件及其祖先目录作为输出。
VSGXCHANGE_DECLSPEC void validateArtifactDirectory(const std::filesystem::path& directory, const std::filesystem::path& sourceRoot);
// 仅写独立制品目录中的指定文件，单写者锁与同目录原子替换保护已有有效内容。
// 临时文件和锁均为本函数管理，失败保留 pending 供排错，不删除输入或未知文件。
// durable 默认强制刷盘；仅可重建运行缓存可设 false，不承诺断电持久，读取仍须校验身份与内容。
VSGXCHANGE_DECLSPEC void publishArtifactText(const std::filesystem::path& directory, const std::filesystem::path& sourceRoot,
    const std::string& filename, std::string_view contents, bool durable = true);
}
