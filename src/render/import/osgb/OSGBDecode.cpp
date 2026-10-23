#include "OSGBDecode.h"
#include "OSGBFiles.h"
#include <osgDB/Callbacks>
#include <osgDB/FileUtils>
#include <osgDB/ObjectWrapper>
#include <osgDB/Options>
#include <osgDB/Registry>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <streambuf>
#ifdef _WIN32
#include <windows.h>
#endif

namespace geostudio::native::io::detail
{
namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
std::mutex admissionMutex;
std::condition_variable admissionChanged;
uint32_t globalInFlight = 0;
// 固定 OSG 每次 InputStream 构造都会写全局 s_lastSchema；嵌入 JPEG 也写全局 jpegerror。
// 预热只消除首次注册，不能消除这些写入，因此此兼容锁覆盖整个第三方解码过程。
std::timed_mutex compatibilityMutex;

struct Cancelled : std::runtime_error
{
    Cancelled() : std::runtime_error("OSGB decode cancelled") {}
};

double milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// 仅在持有兼容锁时调用；插件与常见核心 Wrapper 在第一次读之前完成串行初始化。
void initializePluginsAndWrappers()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto registry = osgDB::Registry::instance();
#ifdef _WIN32
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&initializePluginsAndWrappers), &module))
        {
            wchar_t modulePath[32768];
            const auto length = GetModuleFileNameW(module, modulePath, 32768);
            if (length && length < 32768)
            {
                auto directory = fs::path(modulePath).parent_path().u8string();
                auto& paths = registry->getLibraryFilePathList();
                if (std::find(paths.begin(), paths.end(), directory) == paths.end()) paths.insert(paths.begin(), directory);
            }
        }
#endif
        for (const auto* extension : {"osgb", "png", "jpeg"})
            if (!registry->getReaderWriterForExtension(extension))
                throw std::runtime_error(std::string("OSGB plugin missing: ") + extension);
        auto manager = registry->getObjectWrapperManager();
        for (const auto* name : {"osg::Object", "osg::Group", "osg::Geometry", "osg::Image", "osg::Texture2D", "osg::PagedLOD"})
            if (!manager->findWrapper(name)) throw std::runtime_error(std::string("OSGB serializer missing: ") + name);
        // 复制 Wrapper 引用后预热关联版本表；初始化期间可能加载新的关联 Wrapper。
        std::set<osgDB::ObjectWrapper*> warmed;
        for (;;)
        {
            std::vector<osg::ref_ptr<osgDB::ObjectWrapper>> pending;
            for (const auto& entry : manager->getWrapperMap())
                if (warmed.insert(entry.second.get()).second) pending.push_back(entry.second);
            if (pending.empty()) break;
            for (const auto& wrapper : pending) wrapper->setupAssociatesRevisionsInheritanceIfRequired();
        }
    });
}

// 内存只读流不再复制整份文件；保留插件探测文件头所需的 seek 能力。
class MemoryInputBuffer : public std::streambuf
{
public:
    explicit MemoryInputBuffer(std::string& bytes) { setg(bytes.data(), bytes.data(), bytes.data() + bytes.size()); }
protected:
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode mode) override
    {
        if (!(mode & std::ios_base::in) || (mode & std::ios_base::out)) return pos_type(off_type(-1));
        const auto size = egptr() - eback();
        const auto base = direction == std::ios_base::beg ? 0 : direction == std::ios_base::cur ? gptr() - eback() : size;
        if (offset < -base || offset > size - base) return pos_type(off_type(-1));
        setg(eback(), eback() + base + offset, egptr());
        return pos_type(base + offset);
    }
    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override
    {
        return seekoff(off_type(position), std::ios_base::beg, mode);
    }
};

// 回调属于单次读取，所有调用都位于兼容锁内；仅记录确实成功读取的外部文件，不把嵌入图像名当依赖。
struct ReadDependencies : osgDB::ReadFileCallback
{
    const std::atomic_bool& closed;
    uint64_t maxSourceBytes;
    std::set<fs::path> images;
    std::map<fs::path, std::string> imageHashes;
    std::string failure;
    ReadDependencies(const std::atomic_bool& value, uint64_t limit) : closed(value), maxSourceBytes(limit) {}

    osgDB::ReaderWriter::ReadResult readNode(const std::string&, const osgDB::Options*) override
    {
        failure = "OSGB ProxyNode/external nodes must be loaded through the VSG pager";
        return osgDB::ReaderWriter::ReadResult(failure);
    }
    osgDB::ReaderWriter::ReadResult readImage(const std::string& filename, const osgDB::Options* options) override
    {
        try
        {
            if (closed) throw Cancelled();
            auto resolved = osgDB::findDataFile(filename, options);
            if (resolved.empty()) throw std::runtime_error("OSGB external image missing: " + filename);
            auto path = fs::weakly_canonical(fs::u8path(resolved));
            if (!fs::is_regular_file(path) || fs::file_size(path) > maxSourceBytes)
                throw std::runtime_error("OSGB external image exceeds file limit or is not a regular file: " + path.u8string());
            const auto beforeHash = sha256File(path);
            auto result = osgDB::Registry::instance()->readImageImplementation(path.u8string(), options);
            if (!result.validImage()) throw std::runtime_error("OSGB external image decode failed: " + path.u8string() + ": " + result.statusMessage());
            if (sha256File(path) != beforeHash)
                throw std::runtime_error("OSGB external image changed during decode: " + path.u8string());
            images.insert(path);
            imageHashes[path] = beforeHash;
            return result;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return osgDB::ReaderWriter::ReadResult(failure);
        }
    }
};
}

struct OSGBDecode::Impl
{
    Settings settings;
    Statistics totals;
    std::atomic_bool closed{false};
    explicit Impl(Settings value) : settings(value)
    {
        if (settings.maxInFlight < 1 || settings.maxInFlight > 4 || settings.maxSourceBytes == 0 || settings.maxSourceBytes > 64 * 1024 * 1024)
            throw std::invalid_argument("OSGB decode requires 1..4 in-flight files and a positive file limit <= 64 MiB");
    }
    void checkClosed() const { if (closed) throw Cancelled(); }

    struct Lease
    {
        Impl& owner;
        explicit Lease(Impl& value) : owner(value) {}
        ~Lease()
        {
            {
                std::lock_guard<std::mutex> lock(admissionMutex);
                --owner.totals.inFlight;
                --globalInFlight;
            }
            admissionChanged.notify_all();
        }
    };
};

OSGBDecode::OSGBDecode() : OSGBDecode(Settings{}) {}
OSGBDecode::OSGBDecode(Settings settings) : _impl(std::make_unique<Impl>(settings)) {}
OSGBDecode::~OSGBDecode() = default;

DecodedSource OSGBDecode::read(const fs::path& input)
{
    const auto waiting = Clock::now();
    {
        std::unique_lock<std::mutex> lock(admissionMutex);
        ++_impl->totals.requests;
        admissionChanged.wait(lock, [&] { return _impl->closed ||
            (globalInFlight < 4 && _impl->totals.inFlight < _impl->settings.maxInFlight); });
        _impl->totals.admissionWaitMilliseconds += milliseconds(waiting);
        if (_impl->closed) { ++_impl->totals.cancelled; throw Cancelled(); }
        ++globalInFlight;
        ++_impl->totals.inFlight;
        _impl->totals.peakInFlight = std::max(_impl->totals.peakInFlight, _impl->totals.inFlight);
    }
    Impl::Lease lease(*_impl);
    try
    {
        const auto path = fs::absolute(input).lexically_normal();
        const auto reading = Clock::now();
        std::string bytes;
        try
        {
            if (!fs::is_regular_file(path)) throw std::runtime_error("OSGB file not found: " + path.u8string());
            const auto size = fs::file_size(path);
            if (size == 0 || size > _impl->settings.maxSourceBytes) throw std::runtime_error("OSGB file exceeds configured size limit or is empty: " + path.u8string());
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("OSGB file cannot be opened: " + path.u8string());
            bytes.resize(static_cast<size_t>(size));
            for (size_t offset = 0; offset < bytes.size();)
            {
                _impl->checkClosed();
                const auto count = std::min<size_t>(65536, bytes.size() - offset);
                if (!file.read(bytes.data() + offset, static_cast<std::streamsize>(count)))
                    throw std::runtime_error("OSGB file read failed or source changed: " + path.u8string());
                offset += count;
            }
            if (file.peek() != std::char_traits<char>::eof()) throw std::runtime_error("OSGB source grew while reading: " + path.u8string());
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.ioMilliseconds += milliseconds(reading);
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.ioMilliseconds += milliseconds(reading);
        }
        // 身份绑定实际送入解析器的字节，而非转换完成后可能已变化的文件。
        const auto sourceHash = sha256Bytes(bytes);
        const auto lockStart = Clock::now();
        std::unique_lock<std::timed_mutex> compatibilityLock(compatibilityMutex, std::defer_lock);
        try
        {
            while (!compatibilityLock.try_lock_for(std::chrono::milliseconds(20))) _impl->checkClosed();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.compatibilityLockWaitMilliseconds += milliseconds(lockStart);
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.compatibilityLockWaitMilliseconds += milliseconds(lockStart);
        }
        _impl->checkClosed();
        const auto decoding = Clock::now();
        DecodedSource result;
        try
        {
            initializePluginsAndWrappers();
            auto options = new osgDB::Options;
            osg::ref_ptr<osgDB::Options> owner = options;
            options->setObjectCacheHint(osgDB::Options::CACHE_NONE);
            options->setObjectCache(nullptr);
            options->getDatabasePathList().push_front(path.parent_path().u8string());
            options->setPluginStringData("fileType", "Binary");
            auto callback = new ReadDependencies(_impl->closed, _impl->settings.maxSourceBytes);
            osg::ref_ptr<ReadDependencies> callbackOwner = callback;
            options->setReadFileCallback(callback);
            MemoryInputBuffer buffer(bytes);
            std::istream stream(&buffer);
            auto decoded = osgDB::Registry::instance()->getReaderWriterForExtension("osgb")->readNode(stream, options);
            _impl->checkClosed();
            if (!callback->failure.empty()) throw std::runtime_error(callback->failure);
            if (!decoded.validNode()) throw std::runtime_error("OSGB decode failed: " + path.u8string() + ": " + decoded.statusMessage());
            result.node = decoded.takeNode();
            result.externalImages.assign(callback->images.begin(), callback->images.end());
            result.sourceSha256 = sourceHash;
            result.externalImageHashes = std::move(callback->imageHashes);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.decodeMilliseconds += milliseconds(decoding);
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(admissionMutex);
            _impl->totals.decodeMilliseconds += milliseconds(decoding);
            if (_impl->closed) throw Cancelled();
            ++_impl->totals.completed;
        }
        return result;
    }
    catch (const Cancelled&)
    {
        std::lock_guard<std::mutex> lock(admissionMutex);
        ++_impl->totals.cancelled;
        throw;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(admissionMutex);
        ++_impl->totals.failed;
        throw;
    }
}

void OSGBDecode::close()
{
    {
        std::lock_guard<std::mutex> lock(admissionMutex);
        _impl->closed = true;
    }
    admissionChanged.notify_all();
}

OSGBDecode::Statistics OSGBDecode::statistics() const
{
    std::lock_guard<std::mutex> lock(admissionMutex);
    return _impl->totals;
}
}
