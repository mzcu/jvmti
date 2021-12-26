#include "profile_exporter.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

class FlameGraphProfile : public Profile {
public:
  void AddSample(long allocCount, long allocSize, long usedCount,
                 long usedSize) override;
  void AddLocation(long functionId, long line) override;
  void AddFunction(long id, std::string file, std::string name) override;
  std::vector<unsigned char> Serialize() override;

private:
  std::unordered_map<long, std::string> function_names_;
  // { [allocBytes, usedBytes], [topFrame, line], ..., [bottomFrame, line] }*
  std::vector<std::vector<std::pair<long, long>>> frames_;
};

std::unique_ptr<Profile> Profile::Create() {
  return std::make_unique<FlameGraphProfile>();
}

void FlameGraphProfile::AddSample(long allocCount, long allocSize,
                                  long usedCount, long usedSize) {
  frames_.push_back(std::vector<std::pair<long, long>>());
  frames_.back().push_back({allocSize, usedSize});
}

void FlameGraphProfile::AddLocation(long functionId, long line) {
  frames_.back().push_back({functionId, line});
}

void FlameGraphProfile::AddFunction(long id, std::string file,
                                    std::string name) {
  auto extension_pos = file.find_last_of(".");
  auto class_name =
      extension_pos == std::string::npos ? file : file.substr(0, extension_pos);
  function_names_[id] = class_name + "::" + name;
}

// Format: stackBottom; ...; stackTop size
std::vector<unsigned char> FlameGraphProfile::Serialize() {

  std::stringstream ss;

  for (auto const &stack : frames_) {
    auto [allocBytes, usedBytes] = *stack.begin();
    auto it = stack.rbegin();
    if (usedBytes > 0) {
      while (it != stack.rend() - 1) {
        auto [id, line] = *it;
        ss << function_names_[id] << ":" << line;
        if (it != stack.rend() - 2) {
          ss << ";";
        }
        it += 1;
      }
      ss << " " << usedBytes << std::endl;
    }
  }

  ss.seekg(0, std::ios::beg);
  auto bof = ss.tellg();
  ss.seekg(0, std::ios::end);
  auto stream_size = std::size_t(ss.tellg() - bof);
  ss.seekg(0, std::ios::beg);
  std::vector<unsigned char> buffer(stream_size);
  ss.read((char *)buffer.data(), std::streamsize(buffer.size()));
  return buffer;
}
