#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "ship/resource/File.h"
#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"
#include "ship/resource/ResourceLoader.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceType.h"
#include "ship/resource/factory/BlobFactory.h"
#include "ship/resource/type/Blob.h"
#include "ship/utils/binarytools/endianness.h"

#include "archive_resource_fixtures.h"

namespace {

constexpr size_t kBlobPadding = 16;
const std::string kNestedPrefix = "->";

std::string HeaderedBlob(const std::string& payload) {
    std::string body(OTR_HEADER_SIZE + sizeof(uint32_t), '\0');
    body[0] = static_cast<char>(Ship::Endianness::Native);
    const uint32_t type = static_cast<uint32_t>(Ship::ResourceType::Blob);
    std::memcpy(body.data() + 4, &type, sizeof(uint32_t));
    const uint32_t size = static_cast<uint32_t>(payload.size());
    std::memcpy(body.data() + OTR_HEADER_SIZE, &size, sizeof(uint32_t));
    return body + payload;
}

// A blob whose payload is "->path" loads that path from inside its own load, the way a display list loads the
// textures it names.
class NestingBlobFactory final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override {
        auto blob = std::dynamic_pointer_cast<Ship::Blob>(mBlobs.ReadResource(file, initData));
        if (blob == nullptr || blob->Data.size() < kBlobPadding) {
            return nullptr;
        }
        const std::string payload(blob->Data.begin(), blob->Data.end() - kBlobPadding);
        if (!payload.starts_with(kNestedPrefix)) {
            return blob;
        }
        return manager->LoadResource(payload.substr(kNestedPrefix.size()));
    }

    Ship::ResourceManager* manager = nullptr;

  private:
    Ship::ResourceFactoryBinaryBlobV0 mBlobs;
};

} // namespace

// The pool's only worker loads a.bin, which loads b.bin. Queueing b.bin behind the worker that is waiting for it never
// finishes.
TEST(ResourceManagerNestedLoad, ALoadFromInsideAFactoryOnAOneWorkerPoolCompletes) {
    LusTest::TempDirectoryArchive base;
    auto threadPool = std::make_shared<Ship::ThreadPool>(1);
    auto manager = std::make_shared<Ship::ResourceManager>(threadPool);
    manager->Init({ { "archivePaths", std::vector<std::string>{ base.GetPath().string() } },
                    { "validHashes", std::vector<uint32_t>{} } });
    auto factory = std::make_shared<NestingBlobFactory>();
    factory->manager = manager.get();
    manager->GetResourceLoader()->RegisterResourceFactory(factory, RESOURCE_FORMAT_BINARY, "Blob",
                                                          static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
    manager->GetArchiveManager()->AddArchive(LusTest::LoadedArchive(
        "ram://nested", { { "a.bin", HeaderedBlob("->b.bin") }, { "b.bin", HeaderedBlob("leaf") } }));

    std::promise<std::shared_ptr<Ship::IResource>> loaded;
    auto result = loaded.get_future();
    std::thread caller([&] { loaded.set_value(manager->LoadResource("a.bin")); });

    const bool finished = result.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    if (!finished) {
        caller.detach();
        FAIL() << "loading a.bin deadlocked on the one-worker pool";
    }
    caller.join();

    auto blob = std::dynamic_pointer_cast<Ship::Blob>(result.get());
    ASSERT_NE(blob, nullptr);
    EXPECT_EQ(std::string(blob->Data.begin(), blob->Data.end() - kBlobPadding), "leaf");
}
