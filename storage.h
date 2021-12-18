#ifndef STORAGE_H_
#define STORAGE_H_

// {{{ Includes
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <jvmti.h>

#include <fstream>
#include <map>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>
//  }}}

// {{{ Data
struct AllocationInfo {
  long sizeBytes;
  uintptr_t ref;
};

struct MethodInfo {
  std::string name;
  std::string klass;
  std::string file;
  int line;
};

inline std::ostream &operator<<(std::ostream &os, const MethodInfo &m) {
  return (os << m.klass << m.name << "(" << m.file << ":" << m.line << ")");
}

class StackTrace {
public:
  // TODO: expose necessary iterator instead of vector
  std::vector<uintptr_t> GetFrames() const { return frames; }
  void AddFrame(uintptr_t methodId) { frames.push_back(methodId); };

private:
  std::vector<uintptr_t> frames;
};

inline std::ostream &operator<<(std::ostream &os, const StackTrace &st) {
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
  std::unordered_map<uintptr_t, MethodInfo> methods;
  void AddMethod(uintptr_t id, MethodInfo methodInfo) {
    methods.insert({id, methodInfo});
  }
  void AddAllocation(long id, StackTrace stackTrace,
                     AllocationInfo allocationInfo) {
    if (!stacks.contains(id)) {
      stacks.insert({id, stackTrace});
    }
    allocations.insert({id, allocationInfo});
  }
  bool HasMethod(uintptr_t id) const { return methods.contains(id); }
  MethodInfo GetMethod(uintptr_t id) { return methods[id]; }
  StackTrace GetStackTrace(long id) { return stacks[id]; }

private:
  std::unordered_map<long, StackTrace> stacks;
};

// }}}

#endif // STORAGE_H_
