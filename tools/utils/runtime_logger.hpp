

#ifndef RUNTIME_LOGGER_HPP
#define RUNTIME_LOGGER_HPP

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if __has_include(<filesystem>)
#include <filesystem>
#endif

class RuntimeLogger {
 private:
  using clock = std::chrono::steady_clock;

  static double secs(clock::duration d) {
    return std::chrono::duration<double>(d).count();
  }

  static std::string fmt(double s) {
    std::ostringstream oss;
    if (s < 60.0) {
      oss << std::fixed << std::setprecision(2) << s << "s";
    } else if (s < 3600.0) {
      int m = static_cast<int>(s) / 60;
      double rem = s - m * 60;
      oss << m << "m " << std::fixed << std::setprecision(2) << rem << "s";
    } else {
      int h = static_cast<int>(s) / 3600;
      int m = (static_cast<int>(s) % 3600) / 60;
      double rem = s - h * 3600 - m * 60;
      oss << h << "h " << m << "m " << std::fixed << std::setprecision(2) << rem << "s";
    }
    return oss.str();
  }

  static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
  }

  void log(const std::string& msg) {
    std::cerr << msg << "\n";
    std::ofstream f(log_path_, std::ios::app);
    if (f) f << msg << "\n";
  }

  std::string tool_name_;
  std::string log_path_;
  clock::time_point start_;
  std::vector<std::pair<std::string, double>> steps_;

 public:
  explicit RuntimeLogger(const std::string& tool_name)
      : tool_name_(tool_name), start_(clock::now()) {

    const char* env = std::getenv("NLCELL_RUNTIME_LOG");
    if (env && env[0] != '\0') {
#if __has_include(<filesystem>)
      std::filesystem::create_directories(env);
#endif
      log_path_ = std::string(env) + "/" + tool_name + ".runtime.log";
    } else {
      log_path_ = tool_name + ".runtime.log";
    }

    { std::ofstream f(log_path_, std::ios::trunc); }
    log("[" + tool_name_ + "] Started at " + timestamp());
  }

  class StepGuard {
   public:
    StepGuard(RuntimeLogger& rl, std::string name)
        : rl_(rl), name_(std::move(name)), start_(clock::now()), moved_(false) {
      rl_.log("  [" + name_ + "] started");
    }
    ~StepGuard() {
      if (moved_) return;
      double elapsed = secs(clock::now() - start_);
      rl_.steps_.emplace_back(name_, elapsed);
      rl_.log("  [" + name_ + "] done in " + fmt(elapsed));
    }
    StepGuard(StepGuard&& o) noexcept
        : rl_(o.rl_), name_(std::move(o.name_)), start_(o.start_), moved_(false) {
      o.moved_ = true;
    }
    StepGuard(const StepGuard&) = delete;
    StepGuard& operator=(const StepGuard&) = delete;
    StepGuard& operator=(StepGuard&&) = delete;

   private:
    RuntimeLogger& rl_;
    std::string name_;
    clock::time_point start_;
    bool moved_ = false;
  };

  [[nodiscard]] StepGuard step(const std::string& name) {
    return StepGuard(*this, name);
  }

  void done() {
    double total = secs(clock::now() - start_);
    log("");
    log("[" + tool_name_ + "] Runtime Summary:");
    for (const auto& [name, elapsed] : steps_) {
      std::ostringstream oss;
      oss << "  " << std::left << std::setw(40) << name
          << " " << std::right << std::setw(12) << fmt(elapsed);
      log(oss.str());
    }
    {
      std::ostringstream oss;
      oss << "  " << std::left << std::setw(40) << "TOTAL"
          << " " << std::right << std::setw(12) << fmt(total);
      log(oss.str());
    }
    log("[" + tool_name_ + "] Finished at " + timestamp());
  }

};

#endif
