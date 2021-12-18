#ifndef LOG_H_
#define LOG_H_

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string_view>
#define LOG_INFO(STREAM)                                                       \
  ScopedLogger("INFO ", __FILE__, __LINE__).stream() << STREAM;
#define LOG_ERROR(STREAM)                                                      \
  ScopedLogger("ERROR", __FILE__, __LINE__).stream() << STREAM;

#ifdef DEBUG
#define LOG_DEBUG(STREAM)                                                      \
    ScopedLogger("DEBUG", __FILE__, __LINE__).stream() << STREAM;
#else
#define LOG_DEBUG(STREAM)
#endif // DEBUG

#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

struct ScopedLogger {
  ScopedLogger(std::string_view level, std::string_view file, unsigned line)
      : level_(level) {
    ss_ << level << " - " << file << ":" << std::setw(4) << std::setfill('0')
        << line << " [" << std::this_thread::get_id() << "] ";
  }

  std::stringstream &stream() { return ss_; }
  ~ScopedLogger() {
    auto now = std::chrono::system_clock::now();
    auto us = duration_cast<std::chrono::microseconds>(now.time_since_epoch())
                  .count() %
              1000000;
    auto time = std::chrono::system_clock::to_time_t(now);
    auto gmt_time = gmtime(&time);
    (level_ != "ERROR" ? std::cout : std::cerr)
        << std::put_time(gmt_time, "%Y-%m-%dT%H:%M:%S.") << std::setw(6)
        << std::setfill('0') << us << " - " << ss_.str();
  }

private:
  std::stringstream ss_;
  const std::string_view level_;
};

#endif // LOG_H_
