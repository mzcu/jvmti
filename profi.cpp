// {{{ Includes
#include <jvmti.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "google/protobuf/stubs/common.h"
#include "profile.pb.h"
//  }}}

// {{{ Forward declarations
extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size);
// }}}

// {{{ Data
struct AllocationInfo {
  jlong sizeBytes;
  jweak ref;
};

struct MethodInfo {
  std::string name;
  std::string klass;
  std::string file;
  int line;
};

std::ostream &operator<<(std::ostream &os, const MethodInfo &m) {
  return (os << m.klass << m.name << "(" << m.file << ":" << m.line << ")");
}

class StackTrace {
public:
  // TODO: expose necessary iterator instead of vector
  std::vector<jmethodID> GetFrames() const { return frames; }
  void AddFrame(jmethodID methodId) { frames.push_back(methodId); };

private:
  std::vector<jmethodID> frames;
};

std::ostream &operator<<(std::ostream &os, const StackTrace &st) {
  std::stringstream sstream;
  for (auto id : st.GetFrames()) {
    sstream << std::hex << id << " ";
  }
  return (os << sstream.str());
}

class Storage {
public:
  // TODO: hide fields and expose necessary iterators
  std::multimap<long, AllocationInfo> allocations;
  std::map<jmethodID, MethodInfo> methods;
  void AddMethod(jmethodID id, MethodInfo methodInfo) {
    methods.insert({id, methodInfo});
  }
  void AddAllocation(long id, StackTrace stackTrace,
                     AllocationInfo allocationInfo) {
    if (!stacks.contains(id)) {
      stacks.insert({id, stackTrace});
    }
    allocations.insert({id, allocationInfo});
  }
  bool HasMethod(jmethodID id) const { return methods.contains(id); }
  MethodInfo GetMethod(jmethodID id) { return methods[id]; }
  StackTrace GetStackTrace(long id) { return stacks[id]; }

private:
  std::map<long, StackTrace> stacks;
};

// }}}

static std::mutex write;
static Storage storage;

// {{{ OnLoad Callback
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
// }}}

bool isDeallocated(JNIEnv *jni, jweak ref) {
  return jni->IsSameObject(ref, NULL);
}

void exportHeapProfileProtobuf(
    std::function<bool(const jweak &)> allocChecker) {

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
  for (auto allocation : storage.allocations) {
    auto stackId = allocation.first;
    auto allocationInfo = allocation.second;
    // Requires sentinel as the last element
    if (stackId != currentStackId && currentAllocCount > 0) {
      auto stack = storage.GetStackTrace(currentStackId);
      auto sample = profile.add_sample();
      sample->add_value(currentAllocCount);
      sample->add_value(currentAllocSize);
      sample->add_value(currentUsedCount);
      sample->add_value(currentUsedSize);
      for (auto methodId : stack.GetFrames()) {
        auto address = reinterpret_cast<long>(methodId);
        auto method = storage.GetMethod(methodId);
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
      currentUsedSize = currentUsedCount = currentAllocSize =
          currentAllocCount = 0;
    }

    currentAllocCount++;
    currentAllocSize += allocationInfo.sizeBytes;

    if (allocChecker(allocationInfo.ref)) {
      currentUsedCount++;
      currentUsedSize += allocationInfo.sizeBytes;
    }
  }

  int sti = 7;
  for (auto method : storage.methods) {
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

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  JNIEnv *jni = NULL;
  vm->GetEnv((void **)&jni, JNI_VERSION_10);
  auto allocChecker = [jni](jweak ref) { return isDeallocated(jni, ref); };
  exportHeapProfileProtobuf(allocChecker);
}

void check(jvmtiError err, const char *msg) {
  if (err != JVMTI_ERROR_NONE) {
    std::cout << msg << std::endl;
    exit(err);
  }
}


// {{{ SampledObjectAlloc callback
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
    StackTrace stack;
    long hash = 0;

    for (auto i = 0; i < frame_count; i++) {
      jmethodID method = frames[i].method;
      jlocation location = frames[i].location;

      // from heapster / gperftools
      hash += reinterpret_cast<uintptr_t>(method);
      hash += hash << 10;
      hash ^= hash >> 6;

      stack.AddFrame(method);

      {
        const std::lock_guard<std::mutex> lock(write);
        if (storage.HasMethod(method))
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
        storage.AddMethod(method, *info);
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
    AllocationInfo *info = new AllocationInfo{.sizeBytes = size, .ref = ref};

    {
      const std::lock_guard<std::mutex> lock(write);
      // std::cout << "alloc " << allocatedKlassName << " in " << methods[top]
      // << std::endl;
      storage.AddAllocation(hash, stack, *info);
    }
  }
}
// }}}
