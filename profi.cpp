#include <cstdio>
#include <iostream>
#include <jvmti.h>
#include <map>
#include <ostream>


extern "C" JNIEXPORT void JNICALL SampledObjectAlloc(jvmtiEnv *env, JNIEnv *jni,
                                                     jthread thread,
                                                     jobject object,
                                                     jclass klass, jlong size);
struct allocationInfo {
  jlong sizeBytes;
  jweak ref;
};

static std::multimap<std::string, allocationInfo> storage = {};
static std::mutex write;

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options,
                                    void *reserved) {

  std::cout << "\nHello there\n";

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

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  JNIEnv *jni = NULL;
  vm->GetEnv((void **)&jni, JNI_VERSION_10);
  std::cout << "\nBye!\n";
  std::string currentKlass = storage.begin()->first;
  jlong currentSize = 0;
  jlong currentReclaimed = 0;
  for (const auto &pair : storage) {
    if (pair.first != currentKlass) {
      if (currentSize + currentReclaimed > 1024) {
        std::cout << currentKlass << " retained=" << currentSize
             << " reclaimed=" << currentReclaimed << "\n";
      }
      currentKlass = pair.first;
      currentSize = currentReclaimed = 0;
    }

    if (isDeallocated(jni, pair.second.ref)) {
      currentReclaimed += pair.second.sizeBytes;
    } else {
      currentSize += pair.second.sizeBytes;
    }

  }
  if (currentSize + currentReclaimed > 1024) {
    std::cout << "last: " << currentKlass << " retained=" << currentSize
         << " reclaimed=" << currentReclaimed << "\n";
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
    jweak ref = jni->NewWeakGlobalRef(object);
    char *signature;
    env->GetClassSignature(klass, &signature, nullptr);
    std::string signatureAsString(signature);
    allocationInfo *info = new allocationInfo{.sizeBytes = size, .ref = ref};
    const std::lock_guard<std::mutex> lock(write);
    storage.insert(std::pair<std::string, allocationInfo>(signatureAsString, *info));
    // std::cout << std::flush << "\n(" << signatureAsString << " +" << size << ")";
    env->Deallocate((unsigned char *)signature);
  }
}
