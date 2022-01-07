#ifndef PROFILE_EXPORTER_H_
#define PROFILE_EXPORTER_H_

#include "storage.h"
#include <functional>
#include <vector>
#include <memory>

class Profile {
public:
  static std::unique_ptr<Profile> Create();
  virtual ~Profile() {}
  virtual void AddSample(long allocCount, long allocSize, long usedCount,
                         long usedSize) = 0;
  virtual void AddLocation(long functionId, long line) = 0;
  virtual void AddFunction(long id, std::string file, std::string name) = 0;
  virtual std::vector<unsigned char> Serialize() = 0;
};

class ProfileExporter {
public:
  ProfileExporter(Storage &storage) : storage_(storage) {}
  /**
   * Exports heap profile to a sequence of bytes
   *
   * @param objectRefCallback operation to run on stored object references.
   * Callback should return true if object is still in use.
   *
   */
  std::vector<unsigned char>
  ExportHeapProfile(std::function<bool(uintptr_t)> objectRefCallback) {

    auto profile = Profile::Create();

    if (storage_.allocations.empty()) {
      return std::vector<unsigned char>(0);
    }

    auto currentStackId = storage_.allocations.begin()->first;
    auto sentinel = currentStackId + 1; // fake last element
    storage_.allocations.insert({sentinel, AllocationInfo{}});
    jlong currentAllocSize = 0;
    jlong currentAllocCount = 0;
    jlong currentUsedSize = 0;
    jlong currentUsedCount = 0;
    auto allocation = storage_.allocations.begin();
    while (allocation != storage_.allocations.end()) {
      auto stackId = allocation->first;
      auto allocationInfo = allocation->second;
      storage_.allocations.erase(allocation++);
      // Requires sentinel as the last element
      if (stackId != currentStackId && currentAllocCount > 0) {
        auto stack = storage_.GetStackTrace(currentStackId);
        profile->AddSample(currentAllocCount, currentAllocSize,
                           currentUsedCount, currentUsedSize);
        for (auto const &methodId : stack.GetFrames()) {
          auto method = storage_.GetMethod(methodId);
          profile->AddLocation(methodId, method.line);
        }
        currentStackId = stackId;
        currentUsedSize = currentUsedCount = currentAllocSize =
            currentAllocCount = 0;
      }

      currentAllocCount++;
      currentAllocSize += allocationInfo.sizeBytes;

      if (objectRefCallback(allocationInfo.ref)) {
        currentUsedCount++;
        currentUsedSize += allocationInfo.sizeBytes;
      }
    }

    for (auto const &method : storage_.methods) {
      profile->AddFunction(method.first, method.second.file,
                           method.second.name);
    }

    return profile->Serialize();
  }

private:
  Storage &storage_;
};

#endif // PROFILE_EXPORTER_H_
