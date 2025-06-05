#ifndef LOGGER_H
#define LOGGER_H

#include <cassert>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "llvm/Support/Process.h"

class Logger {
  std::ofstream LogFile;

  std::string getCurrentTimestamp() {
    auto Now = std::chrono::system_clock::now();
    auto NowC = std::chrono::system_clock::to_time_t(Now);
    std::tm NowTm;
    localtime_r(&NowC, &NowTm);
    std::stringstream SS;
    SS << std::put_time(&NowTm, "%Y-%m-%d %H:%M:%S");
    return SS.str();
  }

public:
  Logger(const std::string &Filename) : LogFile(Filename, std::ios::app) {
    if (!LogFile.is_open()) {
      std::string FailureMsg =
          "Failed to open log file due to errno " + std::to_string(errno);
      assert(false && FailureMsg.c_str());
    }
  }
  ~Logger() { LogFile.close(); }

  void log(const std::string &Message) {
    LogFile << "[" << getCurrentTimestamp() << "] "
            << "[pid=" << llvm::sys::Process::getProcessId() << "] " << Message
            << std::endl;
  }

  void error(const std::string &Message) {
    LogFile << "[" << getCurrentTimestamp() << "] [ERROR] " << Message
            << std::endl;
    assert(false);
  }
};

#endif // LOGGER_H
