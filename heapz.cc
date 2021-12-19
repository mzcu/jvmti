// {{{ Includes
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <jvmti.h>

#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "google/protobuf/stubs/common.h"
#include "profile.pb.h"

#include "heapz-inl.h"
#include "log.h"
#include "storage.h"
//  }}}

// {{{ Forward declarations
extern "C" {
JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *, JNIEnv *, jthread,
                                          jobject, jclass, jlong);
JNIEXPORT void JNICALL VMStart(jvmtiEnv *, JNIEnv *);
}
// }}}

static std::mutex write;
static Storage storage;
static volatile bool isProfiling = false;
static std::function<bool(long)> setSamplingInterval;
static std::function<void(void)> forceGarbageCollection;

static bool isObjectAllocated(JNIEnv* env, uintptr_t ref) {
    return !env->IsSameObject(reinterpret_cast<jweak>(ref), NULL);
}

static void deleteWeakGlobalRef(JNIEnv* env, uintptr_t ref) {
    return env->DeleteWeakGlobalRef(reinterpret_cast<jweak>(ref));
}

// {{{ OnLoad Callback
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options,
                                    void *reserved) {

  jvmtiEnv *jvmti = NULL;

  if ((jvm->GetEnv((void **)&jvmti, JVMTI_VERSION_11)) != JNI_OK ||
      jvmti == NULL) {
    LOG_ERROR("Need JVMTI version >= 11")
    exit(1);
  }

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.SampledObjectAlloc = &SampledObjectAlloc;
  callbacks.VMStart = &VMStart;

  jvmtiCapabilities caps;
  memset(&caps, 0, sizeof(caps));
  caps.can_generate_sampled_object_alloc_events = 1;
  caps.can_get_line_numbers = 1;
  caps.can_get_source_file_name = 1;
  if (JVMTI_ERROR_NONE != jvmti->AddCapabilities(&caps)) {
    return JNI_ERR;
  }

  if (JVMTI_ERROR_NONE !=
      jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                      JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, NULL)) {
    return JNI_ERR;
  }

  if (JVMTI_ERROR_NONE != jvmti->SetEventNotificationMode(
                              JVMTI_ENABLE, JVMTI_EVENT_VM_START, NULL)) {
    return JNI_ERR;
  }

  if (JVMTI_ERROR_NONE !=
      jvmti->SetEventCallbacks(&callbacks, sizeof(jvmtiEventCallbacks))) {
    return JNI_ERR;
  }

  setSamplingInterval = [jvmti](long interval) {
    auto result = jvmti->SetHeapSamplingInterval(interval);
    if (JVMTI_ERROR_NONE != result) {
      std::cout << "JVMTI error when setting heap sampling interval: " << result
                << std::endl;
      return false;
    }
    LOG_INFO("Heap sampling interval set to " << result << "k" << std::endl)
    return true;
  };

  forceGarbageCollection = [jvmti]() {
    auto result = jvmti->ForceGarbageCollection();
    if (result != JVMTI_ERROR_NONE) {
      LOG_ERROR("Can't force GC, JVMTI error code " << result << std::endl)
    }
  };

  setSamplingInterval(0);

  return JNI_OK;
}
// }}}

std::vector<unsigned char> exportHeapProfileProtobuf(JNIEnv* env) {

  LOG_DEBUG("Starting heap sample export" << std::endl)

  LOG_DEBUG("Forcing GC" << std::endl)
  forceGarbageCollection();
  LOG_DEBUG("Forcing GC completed" << std::endl)

  const std::lock_guard<std::mutex> lock(write);

  if (storage.allocations.empty()) {
    return std::vector<unsigned char>(0);
  }

  perftools::profiles::Profile profile;
  profile.add_string_table("");
  {
    auto sampleType = profile.add_sample_type();
    profile.add_string_table("alloc_objects");
    sampleType->set_type(1);
    profile.add_string_table("count");
    sampleType->set_unit(2);
  }
  {
    auto sampleType = profile.add_sample_type();
    profile.add_string_table("alloc_space");
    sampleType->set_type(3);
    profile.add_string_table("bytes");
    sampleType->set_unit(4);
  }
  {
    auto sampleType = profile.add_sample_type();
    profile.add_string_table("inuse_objects");
    sampleType->set_type(5);
    sampleType->set_unit(2); // count
  }
  {
    auto sampleType = profile.add_sample_type();
    profile.add_string_table("inuse_space");
    sampleType->set_type(6);
    sampleType->set_unit(4); // bytes
  }

  int lidx = 1;
  auto currentStackId = storage.allocations.begin()->first;
  auto sentinel = currentStackId + 1; // fake last element
  storage.allocations.insert({sentinel, AllocationInfo{}});
  jlong currentAllocSize = 0;
  jlong currentAllocCount = 0;
  jlong currentUsedSize = 0;
  jlong currentUsedCount = 0;
  auto allocation = storage.allocations.begin();
  while (allocation != storage.allocations.end()) {
    auto stackId = allocation->first;
    auto allocationInfo = allocation->second;
    storage.allocations.erase(allocation++);
    // Requires sentinel as the last element
    if (stackId != currentStackId && currentAllocCount > 0) {
      auto stack = storage.GetStackTrace(currentStackId);
      auto sample = profile.add_sample();
      sample->add_value(currentAllocCount);
      sample->add_value(currentAllocSize);
      sample->add_value(currentUsedCount);
      sample->add_value(currentUsedSize);
      for (auto const& methodId : stack.GetFrames()) {
        auto method = storage.GetMethod(methodId);
        auto location = profile.add_location();
        location->set_id(lidx);
        location->set_address(methodId);
        auto line = location->add_line();
        line->set_function_id(methodId);
        line->set_line(method.line);
        sample->add_location_id(lidx);
        lidx++;
      }
      currentStackId = stackId;
      currentUsedSize = currentUsedCount = currentAllocSize =
          currentAllocCount = 0;
    }

    currentAllocCount++;
    currentAllocSize += allocationInfo.sizeBytes;

    if (isObjectAllocated(env, allocationInfo.ref)) {
      currentUsedCount++;
      currentUsedSize += allocationInfo.sizeBytes;
    }

    deleteWeakGlobalRef(env, allocationInfo.ref);

  }

  int sti = 7;
  for (auto const& method : storage.methods) {
    auto function = profile.add_function();
    function->set_id(method.first);
    profile.add_string_table(method.second.file);
    function->set_filename(sti++);
    profile.add_string_table(method.second.name);
    function->set_name(sti);
    function->set_system_name(sti);
    sti++;
  }

  // std::ofstream file;
  // file.open("tmp.prof");
  // profile.SerializeToOstream(&file);
  // file.close();

  auto size = profile.ByteSizeLong();
  auto buffer = std::vector<unsigned char>(size);
  profile.SerializeToArray(buffer.data(), size);

  // std::cout << profile.DebugString() << std::endl;
  google::protobuf::ShutdownProtobufLibrary();

  LOG_DEBUG("Heap sample export completed" << std::endl)
  return buffer;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  LOG_INFO("Unloading heapz agent" << std::endl)
}

void check(jvmtiError err, const char *msg) {
  if (err != JVMTI_ERROR_NONE) {
    std::cout << msg << std::endl;
    exit(err);
  }
}

JNIEXPORT void JNICALL VMStart(jvmtiEnv *jvmti, JNIEnv *env) {

  jclass klass = env->DefineClass("Heapz", NULL, (jbyte const *)Heapz_class,
                                  Heapz_class_len);
  if (klass == NULL) {
    LOG_ERROR("Can't define Heapz.java class" << std::endl)
    exit(2);
  }
}

// {{{ SampledObjectAlloc callback
extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size) {

  if (!isProfiling)
    return;

  jvmtiFrameInfo frames[32];
  jint frame_count;
  jvmtiError err;

  err = env->GetStackTrace(NULL, 0, 32, frames, &frame_count);
  if (err == JVMTI_ERROR_NONE && frame_count >= 1) {

    char *allocatedInstanceClassSignature;
    err = env->GetClassSignature(klass, &allocatedInstanceClassSignature,
                                 nullptr);
    check(err, "alloc class sig");
    std::string allocatedKlassName(allocatedInstanceClassSignature);
    env->Deallocate((unsigned char *)allocatedInstanceClassSignature);

    jmethodID top = frames[0].method;
    StackTrace stack;
    long hash = 0;

    for (auto i = 0; i < frame_count; i++) {
      jmethodID method = frames[i].method;
      jlocation location = frames[i].location;
      uintptr_t methodPointer = reinterpret_cast<uintptr_t>(method);

      // from heapster / gperftools
      hash += reinterpret_cast<uintptr_t>(method);
      hash += hash << 10;
      hash ^= hash >> 6;

      stack.AddFrame(methodPointer);

      {
        const std::lock_guard<std::mutex> lock(write);
        if (storage.HasMethod(methodPointer))
          continue;
      }

      jint lineCount;
      jvmtiLineNumberEntry *lineTable;
      int lineNumber = 0; // remains zero for native code
      err = env->GetLineNumberTable(method, &lineCount, &lineTable);
      if (err == JVMTI_ERROR_NONE) {
        lineNumber = lineTable[0].line_number;
        for (int i = 1; i < lineCount; i++) {
          if (location < lineTable[i].start_location) {
            break;
          }
          lineNumber = lineTable[i].line_number;
        }
        env->Deallocate((unsigned char *)lineTable);
      }

      char *methodName;
      char *methodSignature;
      err = env->GetMethodName(method, &methodName, &methodSignature, nullptr);
      check(err, "meth name");
      jclass methodDeclaringClass;
      err = env->GetMethodDeclaringClass(method, &methodDeclaringClass);
      check(err, "decl class");
      char *methodDeclaringClassSignature;
      err = env->GetClassSignature(methodDeclaringClass,
                                   &methodDeclaringClassSignature, nullptr);
      check(err, "class sig");
      char *sourceFileName;
      err = env->GetSourceFileName(methodDeclaringClass, &sourceFileName);
      if (err != JVMTI_ERROR_NONE) {
        sourceFileName = new char[8];
        strcpy(sourceFileName, "Unknown");
      }
      // check(err, "source name");
      std::string name(methodName);
      std::string klassName(methodDeclaringClassSignature);
      std::string sourceName(sourceFileName);
      MethodInfo *info = new MethodInfo{.name = name,
                                        .klass = klassName,
                                        .file = sourceName,
                                        .line = lineNumber};
      {
        // TODO: use different lock
        const std::lock_guard<std::mutex> lock(write);
        storage.AddMethod(methodPointer, *info);
      }

      env->Deallocate((unsigned char *)methodName);
      env->Deallocate((unsigned char *)methodSignature);
      env->Deallocate((unsigned char *)methodDeclaringClassSignature);
      env->Deallocate((unsigned char *)sourceFileName);
      jni->DeleteLocalRef(methodDeclaringClass);
    } // end loop

    hash += hash << 3;
    hash ^= hash >> 11;

    jweak ref = jni->NewWeakGlobalRef(object);
    AllocationInfo *info = new AllocationInfo{
        .sizeBytes = size, .ref = reinterpret_cast<uintptr_t>(ref)};

    {
      const std::lock_guard<std::mutex> lock(write);
      // std::cout << "alloc " << allocatedKlassName << " in " << methods[top]
      // << std::endl;
      storage.AddAllocation(hash, stack, *info);
    }
  }
}
// }}}

// {{{ Heapz.java native methods

extern "C" {

/*
 * Class:     Heapz
 * Method:    startSampling
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_Heapz_startSampling(JNIEnv *jni, jclass klass) {
  isProfiling = true;
  LOG_INFO("Started sampling" << std::endl)
}

/*
 * Class:     Heapz
 * Method:    stopSampling
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_Heapz_stopSampling(JNIEnv *jni, jclass klass) {
  isProfiling = false;
  LOG_INFO("Stopped sampling" << std::endl)
}

/*
 * Class:     Heapz
 * Method:    getResults
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_Heapz_getResults(JNIEnv *jni, jclass klass) {
  LOG_INFO("Getting sampling results" << std::endl)
  auto buffer = exportHeapProfileProtobuf(jni);
  auto size = buffer.size();
  jbyteArray result = jni->NewByteArray(size);
  jni->SetByteArrayRegion(result, 0, size,
                          reinterpret_cast<jbyte *>(buffer.data()));
  {
    std::lock_guard<std::mutex> lock(write);
    LOG_DEBUG("Clearing storage" << std::endl)
    storage.Clear();
    LOG_DEBUG("Done clearing storage" << std::endl)
  }
  LOG_INFO("Got results, size is " << size << " bytes" << std::endl)
  return result;
}
}

// }}}
