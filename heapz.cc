// {{{ Includes
#include <algorithm>
#include <array>
#include <atomic>
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
#include <unordered_map>
#include <vector>

#include "heapz-inl.h"
#include "log.h"
#include "profile_exporter.h"
#include "storage.h"
//  }}}

// {{{ Forward declarations
extern "C" {
JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *, JNIEnv *, jthread,
                                          jobject, jclass, jlong);
JNIEXPORT void JNICALL VMStart(jvmtiEnv *, JNIEnv *);
JNIEXPORT void JNICALL VMDeath(jvmtiEnv *, JNIEnv *);
}
// }}}

struct HeapzOptions {
  std::string param_one_shot = "oneshot";
  std::string param_sampling_interval = "interval_bytes=";
  std::string param_max_samples = "max_samples=";
  bool one_shot = false;
  int sampling_interval = 1024;
  int max_samples = 1000000;
};

static std::mutex write;
static std::atomic_bool isProfiling = false;
static Storage storage;
static ProfileExporter exporter(storage);

static std::function<bool(long)> setSamplingInterval;
static std::function<void(void)> forceGarbageCollection;

static HeapzOptions heapz_options;

static void storeAsInt(std::string src, int &dest) {
  int tmp;
  std::istringstream iss(src);
  iss >> tmp;
  if (iss.eof()) {
    dest = tmp;
  }
}

static HeapzOptions parseOptions(char *options) {
  HeapzOptions heapz_options;
  std::string str(options ? options : "");
  std::istringstream iss(str);
  std::vector<std::string> opts;
  while (iss.good()) {
    std::string o;
    std::getline(iss, o, ',');
    opts.push_back(o);
  }
  for (const auto &o : opts) {
    if (o == heapz_options.param_one_shot)
      heapz_options.one_shot = true;
    if (o.rfind(heapz_options.param_sampling_interval, 0) == 0) {
      auto value = o.substr(heapz_options.param_sampling_interval.size());
      storeAsInt(value, heapz_options.sampling_interval);
    }
    if (o.rfind(heapz_options.param_max_samples, 0) == 0) {
      auto value = o.substr(heapz_options.param_max_samples.size());
      storeAsInt(value, heapz_options.max_samples);
    }
  }
  LOG_INFO("Options: interval_bytes="
           << heapz_options.sampling_interval
           << " max_samples=" << heapz_options.max_samples
           << " oneshot=" << heapz_options.one_shot << std::endl)
  return heapz_options;
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

  heapz_options = parseOptions(options);

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.SampledObjectAlloc = &SampledObjectAlloc;
  callbacks.VMStart = &VMStart;
  callbacks.VMDeath = &VMDeath;

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

  if (JVMTI_ERROR_NONE != jvmti->SetEventNotificationMode(
                              JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL)) {
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
    LOG_INFO("Heap sampling interval set to " << interval << " bytes" << std::endl)
    return true;
  };

  forceGarbageCollection = [jvmti]() {
    auto result = jvmti->ForceGarbageCollection();
    if (result != JVMTI_ERROR_NONE) {
      LOG_ERROR("Can't force GC, JVMTI error code " << result << std::endl)
    }
  };

  setSamplingInterval(heapz_options.sampling_interval);

  if (heapz_options.one_shot) {
    isProfiling.store(true, std::memory_order_relaxed);
  }


  return JNI_OK;
}
// }}}

std::vector<unsigned char> exportHeapProfile(JNIEnv *env) {
  LOG_DEBUG("Starting heap sample export" << std::endl)
  LOG_DEBUG("Forcing GC" << std::endl)
  forceGarbageCollection();
  LOG_DEBUG("Forcing GC completed" << std::endl)
  const std::lock_guard<std::mutex> lock(write);
  auto &&buffer = exporter.ExportHeapProfile([env](uintptr_t ref) {
    auto jref = reinterpret_cast<jweak>(ref);
    auto isInUse = !env->IsSameObject(jref, NULL);
    env->DeleteWeakGlobalRef(jref);
    return isInUse;
  });
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

JNIEXPORT void JNICALL VMDeath(jvmtiEnv *jvmti, JNIEnv *env) {
  if (heapz_options.one_shot) {
    LOG_INFO("OneShot profile export on VMDeath" << std::endl)
    auto profile = exportHeapProfile(env);
    std::ofstream outfile("oneshot.prof", std::ios::out | std::ios::binary);
    outfile.write(reinterpret_cast<const char *>(profile.data()),
                  profile.size());
  }
}

// {{{ SampledObjectAlloc callback
extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size) {

  if (!isProfiling.load(std::memory_order_relaxed))
    return;

  if (storage.allocations.size() >= heapz_options.max_samples) {
    isProfiling.store(false, std::memory_order_relaxed);
    LOG_DEBUG("Max samples (" << heapz_options.max_samples
                              << ") exceeded, sampling stopped" << std::endl)
    return;
  }

  const int max_frames = 256;
  jvmtiFrameInfo frames[max_frames];
  jint frame_count;
  jvmtiError err;

  err = env->GetStackTrace(NULL, 0, max_frames, frames, &frame_count);
  if (err == JVMTI_ERROR_NONE && frame_count >= 1) {

    jmethodID top = frames[0].method;
    StackTrace stack;
    long hash = 0;

    for (auto i = 0; i < frame_count; i++) {
      jmethodID method = frames[i].method;
      jlocation location = frames[i].location;
      uintptr_t methodId = reinterpret_cast<uintptr_t>(method);

      // from heapster / gperftools
      hash += reinterpret_cast<uintptr_t>(method);
      hash += hash << 10;
      hash ^= hash >> 6;

      stack.AddFrame(methodId);

      {
        const std::lock_guard<std::mutex> lock(write);
        if (storage.HasMethod(methodId))
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
      std::string sourceName = "Unknown";
      if (err == JVMTI_ERROR_NONE) {
        sourceName = sourceFileName;
        env->Deallocate((unsigned char *)sourceFileName);
      }
      std::string name(methodName);
      std::string klassName(methodDeclaringClassSignature);
      MethodInfo info{.name = name,
                      .klass = klassName,
                      .file = sourceName,
                      .line = lineNumber};
      {
        // TODO: use different lock
        const std::lock_guard<std::mutex> lock(write);
        storage.AddMethod(methodId, info);
      }

      env->Deallocate((unsigned char *)methodName);
      env->Deallocate((unsigned char *)methodSignature);
      env->Deallocate((unsigned char *)methodDeclaringClassSignature);
      jni->DeleteLocalRef(methodDeclaringClass);
    } // end loop

    hash += hash << 3;
    hash ^= hash >> 11;

    jweak ref = jni->NewWeakGlobalRef(object);
    AllocationInfo info{.sizeBytes = size,
                        .ref = reinterpret_cast<uintptr_t>(ref)};

    {
      const std::lock_guard<std::mutex> lock(write);
      storage.AddAllocation(hash, stack, info);
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
  isProfiling.store(true, std::memory_order_relaxed);
  LOG_INFO("Started sampling" << std::endl)
}

/*
 * Class:     Heapz
 * Method:    stopSampling
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_Heapz_stopSampling(JNIEnv *jni, jclass klass) {
  isProfiling.store(false, std::memory_order_relaxed);
  LOG_INFO("Stopped sampling" << std::endl)
}

/*
 * Class:     Heapz
 * Method:    getResults
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_Heapz_getResults(JNIEnv *jni, jclass klass) {
  LOG_INFO("Getting sampling results" << std::endl)
  auto buffer = exportHeapProfile(jni);
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
