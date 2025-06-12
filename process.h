#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <chrono>
#include <atomic>
#include <fstream>
#include <mutex>

enum class ProcessState { Waiting, Running, Finished };

class Process {
public:
    Process(const std::string& name, int total_instructions);
    ~Process();

    void logPrint(const std::string& message, int core, 
                 const std::chrono::system_clock::time_point& time);
    
    std::string name;
    int total_instructions;
    std::atomic<int> remaining_instructions;
    std::atomic<ProcessState> state;
    std::atomic<int> core_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;

private:
    std::ofstream log_file;
    std::string log_file_name;
    std::mutex log_mutex;

    void openLogFile();
};

#endif // PROCESS_H