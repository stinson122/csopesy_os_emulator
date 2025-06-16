#include "process.h"
#include <iomanip>
#include <chrono>
#include <format>

Process::Process(const std::string& name, int total_instructions)
    : name(name), total_instructions(total_instructions),
    remaining_instructions(total_instructions),
    state(ProcessState::Waiting), core_id(-1),
    start_time(std::chrono::system_clock::now()),
    log_file_name(name + ".log") {}

Process::~Process() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

void Process::openLogFile() {
    if (!log_file.is_open()) {
        log_file.open(log_file_name, std::ios::app);
    }
}

void Process::logPrint(const std::string& message, int core,
    const std::chrono::system_clock::time_point& time) {
    std::lock_guard<std::mutex> lock(log_mutex);
    openLogFile();
    auto zt = std::chrono::zoned_time{ std::chrono::current_zone(),
        std::chrono::time_point_cast<std::chrono::seconds>(time) };
    log_file << "(" << std::format("{:%m/%d/%Y %I:%M:%S%p}", zt)
        << ") Core:" << core << " \"" << message << "\"\n";
    log_file.flush();
}