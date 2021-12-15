#include "google/protobuf/stubs/common.h"
#include "profile.pb.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <jvmti.h>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

using std::mutex;

extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size);
struct allocationInfo {
  jlong sizeBytes;
  jweak ref;
};

struct methodInfo {
  std::string name;
  std::string klass;
  std::string file;
  int line;
};

std::ostream &operator<<(std::ostream &os, const methodInfo &m) {
  return (os << m.klass << m.name << "(" << m.file << ":" << m.line << ")");
}

// static std::multimap<std::string, allocationInfo> storage = {};
static std::multimap<long, allocationInfo> storage = {};
static std::map<long, std::vector<jmethodID>> stacks = {};
static std::map<jmethodID, methodInfo> methods = {};
static std::mutex write;

std::ostream &operator<<(std::ostream &os, const std::vector<jmethodID> &v) {
  std::stringstream sstream;
  for (auto id : v) {
    sstream << methods[id] << " ";
  }
  return (os << sstream.str());
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options,
                                    void *reserved) {

  jvmtiEnv *jvmti = NULL;

  if ((jvm->GetEnv((void **)&jvmti, JVMTI_VERSION_11)) != JNI_OK ||
      jvmti == NULL) {
    std::cerr << "Need JVMTI 11";
    exit(1);
  }

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.SampledObjectAlloc = &SampledObjectAlloc;

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

  if (JVMTI_ERROR_NONE !=
      jvmti->SetEventCallbacks(&callbacks, sizeof(jvmtiEventCallbacks))) {
    return JNI_ERR;
  }

  if (JVMTI_ERROR_NONE != jvmti->SetHeapSamplingInterval(0)) {
    return JNI_ERR;
  }

  return JNI_OK;
}

bool isDeallocated(JNIEnv *jni, jweak ref) {
  return jni->IsSameObject(ref, NULL);
}

void exportHeapProfile() {

  // header
  std::string profile = "--- heapz 1 --\nformat = java\nresolution=bytes\n";

  // TODO count size @ 0x00000003 [... 0xStackBottom ] \n

  // TODO  0x00000003 com.example.function003 (Source003.java:103)

  std::cout << profile << std::endl;
}

void exportHeapProfileProtobuf(JNIEnv *jni) {

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
  auto currentStackId = storage.begin()->first;
  auto sentinel = currentStackId + 1; // fake last element
  storage.insert({sentinel, allocationInfo{}});
  jlong currentAllocSize = 0;
  jlong currentAllocCount = 0;
  jlong currentUsedSize = 0;
  jlong currentUsedCount = 0;
  for (auto allocation : storage) {
    auto stackId = allocation.first;
    auto allocationInfo = allocation.second;
    // Requires sentinel as the last element
    if (stackId != currentStackId && currentAllocCount > 0) {
      auto stack = stacks[currentStackId];
      auto sample = profile.add_sample();
      sample->add_value(currentAllocCount);
      sample->add_value(currentAllocSize);
      sample->add_value(currentUsedCount);
      sample->add_value(currentUsedSize);
      for (auto methodId : stack) {
        auto address = reinterpret_cast<long>(methodId);
        auto method = methods[methodId];
        auto location = profile.add_location();
        location->set_id(lidx);
        location->set_address(address);
        auto line = location->add_line();
        line->set_function_id(address);
        line->set_line(method.line);
        sample->add_location_id(lidx);
        lidx++;
      }
      currentStackId = stackId;
      currentUsedSize = currentUsedCount
        = currentAllocSize = currentAllocCount = 0;
    }

    currentAllocCount++;
    currentAllocSize += allocationInfo.sizeBytes;

    if (!isDeallocated(jni, allocationInfo.ref)) {
      currentUsedCount++;
      currentUsedSize += allocationInfo.sizeBytes;
    }
  }

  int sti = 7;
  for (auto method : methods) {
    auto function = profile.add_function();
    function->set_id(reinterpret_cast<long>(method.first));
    profile.add_string_table(method.second.file);
    function->set_filename(sti++);
    profile.add_string_table(method.second.name);
    function->set_name(sti);
    function->set_system_name(sti);
    sti++;
  }

  std::ofstream file;
  file.open("tmp.prof");
  profile.SerializeToOstream(&file);
  file.close();

  // std::cout << profile.DebugString() << std::endl;
  google::protobuf::ShutdownProtobufLibrary();
}

void exportHeapProfilePlain(JNIEnv *jni) {

  std::cout << "\nPrinting heap sample:\n\n";

  auto currentStackId = storage.begin()->first;
  jlong currentSize = 0;
  jlong currentReclaimed = 0;
  for (const auto &pair : storage) {
    if (pair.first != currentStackId) {
      if (currentSize + currentReclaimed > 30) {
        auto methodId = stacks[currentStackId].front();
        auto methodInfo = methods[methodId];
        std::cout << methodInfo << " retained=" << currentSize
                  << " reclaimed=" << currentReclaimed << "\n";
      }
      currentStackId = pair.first;
      currentSize = currentReclaimed = 0;
    }

    if (isDeallocated(jni, pair.second.ref)) {
      currentReclaimed += pair.second.sizeBytes;
    } else {
      currentSize += pair.second.sizeBytes;
    }
  }
  // TODO: last entry missing
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  JNIEnv *jni = NULL;
  vm->GetEnv((void **)&jni, JNI_VERSION_10);
  // exportHeapProfilePlain(jni);
  exportHeapProfileProtobuf(jni);
}

void check(jvmtiError err, const char *msg) {
  if (err != JVMTI_ERROR_NONE) {
    std::cout << msg << std::endl;
    exit(err);
  }
}

extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size) {
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
    std::vector<jmethodID> stack;
    long hash = 0;

    for (auto i = 0; i < frame_count; i++) {
      jmethodID method = frames[i].method;
      jlocation location = frames[i].location;

      // from heapster / gperftools
      hash += reinterpret_cast<uintptr_t>(method);
      hash += hash << 10;
      hash ^= hash >> 6;

      stack.push_back(method);

      {
        const std::lock_guard<std::mutex> lock(write);
        if (methods.contains(method))
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
      methodInfo *info = new methodInfo{.name = name,
                                        .klass = klassName,
                                        .file = sourceName,
                                        .line = lineNumber};
      {
        // TODO: use different lock
        const std::lock_guard<std::mutex> lock(write);
        methods.insert({method, *info});
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
    allocationInfo *info = new allocationInfo{.sizeBytes = size, .ref = ref};

    {
      const std::lock_guard<mutex> lock(write);
      // std::cout << "alloc " << allocatedKlassName << " in " << methods[top] << std::endl;
      storage.insert({hash, *info});
      if (!stacks.contains(hash)) {
        stacks.insert({hash, stack});
      }
    }
  }
}
