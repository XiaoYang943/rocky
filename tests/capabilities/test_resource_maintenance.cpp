#include "../globe/catch.hpp"
#include <geostudio/native/runtime/engine/ResourceMaintenance.h>
#include <vsg/app/TransferTask.h>
#include <vsg/app/Viewer.h>
#include <vsg/core/Array.h>
#include <vsg/core/IntrusiveAllocator.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/vk/Context.h>
#include <atomic>
#include <thread>

namespace
{
    // 独立 allocator 能验证块真实析构，不受其他测试的全局内存干扰。
    struct InspectAllocator : vsg::IntrusiveAllocator
    {
        std::weak_ptr<MemoryBlock> lastBlock(vsg::AllocatorAffinity affinity)
        {
            return allocatorMemoryBlocks[affinity]->memoryBlockWithSpace;
        }
        std::size_t lookupCount() const { return memoryBlocks.size(); }
    };
}

TEST_CASE("allocator retirement releases all internal block owners and preserves live blocks", "[memory][cpu]")
{
    InspectAllocator allocator;
    allocator.setBlockSize(vsg::ALLOCATOR_AFFINITY_DATA, 4096);
    allocator.setBlockSize(vsg::ALLOCATOR_AFFINITY_NODES, 4096);
    auto* retained = allocator.allocate(64, vsg::ALLOCATOR_AFFINITY_NODES);
    auto retainedBlock = allocator.lastBlock(vsg::ALLOCATOR_AFFINITY_NODES);
    for (int round = 0; round != 20; ++round)
    {
        auto* data = allocator.allocate(64, vsg::ALLOCATOR_AFFINITY_DATA);
        auto block = allocator.lastBlock(vsg::ALLOCATOR_AFFINITY_DATA);
        REQUIRE_FALSE(block.expired());
        REQUIRE(allocator.deallocate(data, 64));
        REQUIRE(allocator.deleteEmptyMemoryBlocks() > 0);
        REQUIRE(block.expired());
        REQUIRE_FALSE(retainedBlock.expired());
        REQUIRE(allocator.lookupCount() == 1);
        REQUIRE(allocator.validate());
    }
    REQUIRE(allocator.deallocate(retained, 64));
    REQUIRE(allocator.deleteEmptyMemoryBlocks() > 0);
    REQUIRE(retainedBlock.expired());
    REQUIRE(allocator.lookupCount() == 0);
    REQUIRE(allocator.totalMemorySize() == 0);
}

TEST_CASE("allocator maintenance can run concurrently with allocations", "[memory][cpu]")
{
    InspectAllocator allocator;
    allocator.setBlockSize(vsg::ALLOCATOR_AFFINITY_DATA, 4096);
    std::atomic_bool finished{false};
    std::thread worker([&] {
        for (int i = 0; i != 2000; ++i)
        {
            auto* value = allocator.allocate(96, vsg::ALLOCATOR_AFFINITY_DATA);
            std::fill_n(static_cast<unsigned char*>(value), 96, 0x5a);
            allocator.deallocate(value, 96);
        }
        finished = true;
    });
    while (!finished)
    {
        allocator.deleteEmptyMemoryBlocks();
        allocator.totalReservedSize();
        std::this_thread::yield();
    }
    worker.join();
    allocator.deleteEmptyMemoryBlocks();
    REQUIRE(allocator.lookupCount() == 0);
    REQUIRE(allocator.totalMemorySize() == 0);
}

TEST_CASE("orphan uploads retire without a subsequent upload and keep another layer data", "[memory][cpu]")
{
    auto transfer = vsg::TransferTask::create(nullptr);
    auto retained = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, vsg::ubyteArray::create(64));
    transfer->assign(vsg::ImageInfoList{retained});
    for (int round = 0; round != 20; ++round)
    {
        auto image = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, vsg::ubyteArray::create(128));
        auto buffer = vsg::BufferInfo::create(vsg::Buffer::create(128, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE), 0, 128, vsg::ubyteArray::create(128));
        vsg::observer_ptr<vsg::Data> imageData(image->imageView->image->data);
        vsg::observer_ptr<vsg::Data> bufferData(buffer->data);
        transfer->assign(vsg::ImageInfoList{image});
        transfer->assign(vsg::BufferInfoList{buffer});
        image = {};
        buffer = {};
        REQUIRE(transfer->containsDataToTransfer(vsg::TransferTask::TRANSFER_ALL));
        const auto usage = transfer->releaseUnusedResources();
        REQUIRE(usage.orphanedEntries == 2);
        REQUIRE_FALSE(imageData.ref_ptr());
        REQUIRE_FALSE(bufferData.ref_ptr());
        REQUIRE(transfer->containsDataToTransfer(vsg::TransferTask::TRANSFER_ALL));
    }
    retained = {};
    REQUIRE(transfer->releaseUnusedResources().orphanedEntries == 1);
    REQUIRE_FALSE(transfer->containsDataToTransfer(vsg::TransferTask::TRANSFER_ALL));
    REQUIRE(transfer->releaseUnusedResources().orphanedEntries == 0);
}

TEST_CASE("maintenance diagnostics are viewer scoped and survive layer removal", "[memory][cpu]")
{
    using namespace geostudio::native;
    requestResourceMaintenance(nullptr);
    maintainResources(nullptr);
    REQUIRE(resourceMaintenanceMetrics(nullptr).maintenancePasses == 0);
    auto first = vsg::Viewer::create();
    auto second = vsg::Viewer::create();
    requestResourceMaintenance(first);
    maintainResources(first);
    REQUIRE(resourceMaintenanceMetrics(first).maintenancePasses == 1);
    REQUIRE(resourceMaintenanceMetrics(first).pending);
    REQUIRE(resourceMaintenanceMetrics(second).maintenancePasses == 0);
    vsg::observer_ptr<vsg::Viewer> lifetime(first);
    first = {};
    REQUIRE_FALSE(lifetime.ref_ptr());
}

TEST_CASE("context reset releases image payload queued before record and ray tracing work", "[memory][cpu]")
{
    // 空 Device 只构造 CPU 容器，不创建 Vulkan 设备或调用 record；覆盖编译在 record 前失败的清理状态。
    auto context = vsg::Context::create(nullptr);
    REQUIRE_FALSE(context->device);
    REQUIRE_FALSE(context->requiresWaitForCompletion);
    auto otherLayer = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, vsg::ubyteArray::create(64));
    for (int round = 0; round != 20; ++round)
    {
        auto image = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, vsg::ubyteArray::create(1024));
        vsg::observer_ptr<vsg::Data> imagePayload(image->imageView->image->data);
        context->imageInfosToCopy.emplace_back(image);
        image = {};
        REQUIRE(imagePayload.ref_ptr());

        VkAccelerationStructureGeometryKHR geometry{};
        VkAccelerationStructureBuildGeometryInfoKHR info{};
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        auto build = vsg::BuildAccelerationStructureCommand::create(
            nullptr, info, VkAccelerationStructureKHR{}, std::vector<std::uint32_t>{0});
        vsg::observer_ptr<vsg::Object> buildLifetime(build);
        context->buildAccelerationStructureCommands.emplace_back(build);
        context->scratchBufferSize = 4096;
        build = {};

        context->reset();
        REQUIRE(context->imageInfosToCopy.empty());
        REQUIRE_FALSE(imagePayload.ref_ptr());
        REQUIRE(context->buildAccelerationStructureCommands.empty());
        REQUIRE_FALSE(buildLifetime.ref_ptr());
        REQUIRE(context->scratchBufferSize == 0);
        REQUIRE(otherLayer->imageView->image->data->dataSize() == 64);
    }
    context->reset();
    context = {};
}
