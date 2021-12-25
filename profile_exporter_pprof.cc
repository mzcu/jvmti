#include "google/protobuf/stubs/common.h"
#include "profile.pb.h"

#include "profile_exporter.h"
#include <algorithm>
#include <functional>
#include <memory>

class PProfProfile : public Profile {
public:
  PProfProfile() {
    Retain(""); // profile.proto requirement
    {
      auto sampleType = profile_.add_sample_type();
      sampleType->set_type(Retain("alloc_objects"));
      sampleType->set_unit(Retain("count"));
    }
    {
      auto sampleType = profile_.add_sample_type();
      sampleType->set_type(Retain("alloc_space"));
      sampleType->set_unit(Retain("bytes"));
    }
    {
      auto sampleType = profile_.add_sample_type();
      sampleType->set_type(Retain("inuse_objects"));
      sampleType->set_unit(Retain("count"));
    }
    {
      auto sampleType = profile_.add_sample_type();
      sampleType->set_type(Retain("inuse_space"));
      sampleType->set_unit(Retain("bytes"));
    }
  }
  void AddSample(long allocCount, long allocSize, long usedCount,
                 long usedSize) override;
  void AddLocation(long functionId, long line) override;
  void AddFunction(long id, std::string file, std::string name) override;
  std::vector<unsigned char> Serialize() override;
  int Retain(std::string string) {
    if (seen_strings_.contains(string)) {
      return seen_strings_[string];
    } else {
      profile_.add_string_table(string);
      return seen_strings_[string] = seen_strings_.size();
    }
  }

private:
  perftools::profiles::Profile profile_;
  std::unordered_map<std::string, int> seen_strings_;
  perftools::profiles::Sample *current_sample_;
  int currentLocationId_ = 1;
};

std::unique_ptr<Profile> Profile::Create() {
  return std::make_unique<PProfProfile>();
}

void PProfProfile::AddSample(long allocCount, long allocSize, long usedCount,
                             long usedSize) {
  auto sample = profile_.add_sample();
  sample->add_value(allocCount);
  sample->add_value(allocSize);
  sample->add_value(usedCount);
  sample->add_value(usedSize);
  current_sample_ = sample;
}

void PProfProfile::AddLocation(long functionId, long line) {
  auto location = profile_.add_location();
  location->set_id(currentLocationId_);
  location->set_address(functionId);
  auto sourceLine = location->add_line();
  sourceLine->set_function_id(functionId);
  sourceLine->set_line(line);
  current_sample_->add_location_id(currentLocationId_);
  ++currentLocationId_;
}

void PProfProfile::AddFunction(long id, std::string file, std::string name) {
  auto function = profile_.add_function();
  function->set_id(id);
  function->set_filename(Retain(file));
  function->set_name(Retain(name));
  function->set_system_name(Retain(name));
}

std::vector<unsigned char> PProfProfile::Serialize() {
  auto size = profile_.ByteSizeLong();
  auto buffer = std::vector<unsigned char>(size);
  profile_.SerializeToArray(buffer.data(), size);
  // profile.PrintDebugString();
  google::protobuf::ShutdownProtobufLibrary();
  return buffer;
}
