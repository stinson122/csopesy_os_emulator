#include "scheduler.h"
#include "process.h"
#include "header.h"
#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <fstream>
#include <random>
#include <atomic>
#include <filesystem>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
#include <direct.h>   // for _getcwd
#else
#include <unistd.h>   // for getcwd
#endif

Scheduler* scheduler = nullptr;
bool initialized = false;
std::atomic<uint64_t> cpu_cycles(0);
std::atomic<uint64_t> quantum_counter(0);

struct Config {
    int num_cpu = 4;
    std::string scheduler_type = "fcfs";
    uint64_t quantum_cycles = 5;
    uint64_t batch_frequency = 1;
    uint64_t min_instructions = 1;
    uint64_t max_instructions = 2000;
    uint64_t delay_per_exec = 100;
};

Config readConfig(const std::string& filename, const std::filesystem::path& exe_dir) {
    Config config;
    // First try current directory
    std::ifstream file(filename);

    // If not found, try executable directory
    if (!file.is_open()) {
        std::filesystem::path full_path = exe_dir / filename;
        file.open(full_path);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open config file: " << filename << std::endl;
            std::cerr << "Tried locations:\n1. " << std::filesystem::absolute(filename)
                << "\n2. " << full_path << std::endl;
            return config;
        }
    }

    // ADD THIS LINE: Declare the 'line' variable
    std::string line;  // <-- This is the missing declaration
    // Read the config file line by line

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (line.empty()) continue;

        iss >> key;

        if (key == "num-cpu") {
            iss >> config.num_cpu;
        }
        else if (key == "scheduler") {
            std::string value;
            iss >> value;
            // Remove quotes if present
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            config.scheduler_type = value;
        }
        else if (key == "quantum-cycles") {
            iss >> config.quantum_cycles;
        }
        else if (key == "batch-process-freq") {
            iss >> config.batch_frequency;
        }
        else if (key == "min-ins") {
            iss >> config.min_instructions;
        }
        else if (key == "max-ins") {
            iss >> config.max_instructions;
        }
        else if (key == "delay-per-exec") {
            iss >> config.delay_per_exec;
        }
    }

    return config;
}
/*
void processSMI(Process* p) {
    if (!p) return;

    std::cout << "Process: " << p->name << std::endl;
    int remaining = p->remaining_instructions.load();
    std::cout << "Instructions: " << (p->total_instructions - remaining)
        << "/" << p->total_instructions << std::endl;
    std::cout << "Status: "
        << (p->state == ProcessState::Finished ? "Finished" :
            p->state == ProcessState::Running ? "Running" : "Waiting")
        << std::endl;

    if (p->state == ProcessState::Finished) {
        std::cout << "Finished!" << std::endl;
    }
}*/

void processSMI(Process* p) {
    if (!p) return;

    std::cout << "Process name: " << p->name << std::endl;
    //std::cout << "ID: " << p->core_id << std::endl;
    std::cout << "Logs:" << std::endl;

    // Print log messages
    auto logs = p->getLogMessages();
    for (const auto& log : logs) {
        std::cout << log;
    }

    int remaining = p->remaining_instructions.load();
    std::cout << "\nCurrent instruction line: " << (p->total_instructions - remaining) << std::endl;
    std::cout << "Lines of code: " << p->total_instructions << std::endl;

    if (p->state == ProcessState::Finished) {
        std::cout << "\nFinished!" << std::endl;
    }
}

void viewProcessScreen(const std::string& processName)
{
    Process* p = scheduler->getProcess(processName);
    if (!p) {
        std::cout << "Process " << processName << " not found. Type 'exit' to return to main menu." << std::endl;
        return;
    }

    // Get log messages from process instead of file
    std::vector<std::string> logLines = p->getLogMessages();

    p->log_callback = [&](const std::string& message) {
        logLines.push_back(message);
        //Re-print for every callback
        clearScreen();
        for (const auto& logLine : logLines) {
            std::cout << logLine << (logLine.back() == '\n' ? "" : "\n");
        }
        std::cout << "Type 'exit' to return to main menu" << std::endl;
        std::cout << "Enter a command: " << std::flush;
        };

    std::string command;
    while (true) {
        clearScreen();
        for (const auto& logLine : logLines) {
            std::cout << logLine << (logLine.back() == '\n' ? "" : "\n");
        }

        std::cout << "Type 'exit' to return to main menu" << std::endl;
        std::cout << "Enter a command: " << std::flush;

        std::getline(std::cin, command);
        if (command == "exit") {
            p->log_callback = nullptr;
            clearScreen();
            std::cout << "Back to main menu." << std::endl;
            break;
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }
}

void drawScreen(std::string processName) {
    Process* p = scheduler->getProcess(processName);
    if (!p) {
        std::cout << "Process: " << processName << " (not found)" << std::endl;
    }
    else {
        std::cout << "Process: " << p->name << std::endl;
        int remaining = p->remaining_instructions.load();
        std::cout << "Instruction: " << (p->total_instructions - remaining)
            << "/" << p->total_instructions << std::endl;
    }
    std::cout << "TimeStamp: " << Scheduler::formatTimePoint(std::chrono::system_clock::now()) << std::endl;

    std::string command;
    while (true) {
        std::cout << "Type 'exit' to return to main menu, 'process-smi' for info" << std::endl;
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            clearScreen();
            std::cout << "Back to main menu." << std::endl;
            break;
        }
        else if (command == "process-smi") {
            if (p) {
                processSMI(p);
            }
            else {
                std::cout << "Process not found." << std::endl;
            }
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    std::string command;
    printHeader();

    // Store executable directory
    std::filesystem::path exe_dir;
    if (argc > 0) {
        exe_dir = std::filesystem::path(argv[0]).parent_path();
    }
    /*
    // Start CPU cycle counter thread
    std::thread cycle_counter([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            cpu_cycles++;
        }
        });
    */

    std::thread cycle_counter([]() {
        auto last_time = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
            if (elapsed.count() >= 100) { // Update every 100ms
                cpu_cycles++;
                last_time = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });
    cycle_counter.detach();

    while (true) {
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            if (scheduler) {
                scheduler->stop();
                scheduler->stopBatchProcess();
                delete scheduler;
            }
            std::cout << "exit command recognized. Closing program." << std::endl;
            break;
        }
        else if (command == "clear") {
            clearScreen();
        }
        else if (command == "initialize") {
            if (initialized) {
                std::cout << "Scheduler already initialized." << std::endl;
            }
            else {
                // Pass executable directory to readConfig
                Config config = readConfig("config.txt", exe_dir);
                scheduler = new Scheduler(config.num_cpu);
                scheduler->setSchedulerType(config.scheduler_type);
                scheduler->setQuantumCycles(config.quantum_cycles);
                scheduler->setMinInstructions(config.min_instructions);
                scheduler->setMaxInstructions(config.max_instructions);
                scheduler->setBatchFrequency(config.batch_frequency);
                scheduler->setDelay(config.delay_per_exec);

                scheduler->start();
                initialized = true;
                std::cout << "Scheduler initialized with "
                    << config.num_cpu << " cores." << std::endl;
            }
        }
        else if (command.starts_with("screen ")) {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
                continue;
            }

            std::istringstream iss(command);
            std::string base, flag, processName;
            iss >> base >> flag;

            if (flag == "-ls") {
                scheduler->printStatus(false);
            }
            else {
                iss >> processName;
                if ((flag == "-s" || flag == "-r") && !processName.empty()) {
                    Process* existingProcess = scheduler->getProcess(processName);

                    if (flag == "-s") {
                        // Create new process only if it doesn't exist
                        if (!existingProcess) {
                            std::random_device rd;
                            std::mt19937 gen(rd());
                            std::uniform_int_distribution<uint64_t> dist(
                                scheduler->getMinInstructions(),
                                scheduler->getMaxInstructions()
                            );
                            uint64_t instructions = dist(gen);
                            Process* p = new Process(processName, instructions);
                            scheduler->addProcess(p);
                            std::cout << "Created new process: " << processName << std::endl;
                        }
                        else {
                            std::cout << "Process " << processName << " already exists." << std::endl; continue;
                        }
                    }
                    else if (flag == "-r") {
                        // For -r, only attach if process exists and is not finished
                        if (!existingProcess || existingProcess->state == ProcessState::Finished) {
                            std::cout << "Process " << processName << " not found or finished." << std::endl;
                            continue;
                        }
                    }

                    clearScreen();
                    std::cout << "Displaying process: " << processName << std::endl;
                    
                    if (flag == "-s") {
                        drawScreen(processName);
                    } else if (flag == "-r") {
                        //viewProcessScreen(processName);
                        drawScreen(processName);
                    }
                }
                else {
                    std::cout << "Invalid screen command. Usage: screen -s|-r <name> or screen -ls" << std::endl;
                }
            }
        }
        else if (command == "scheduler-start") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            }
            else {
                scheduler->startBatchProcess();
                std::cout << "Scheduler started generating processes." << std::endl;
            }
        }
        else if (command == "scheduler-stop") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            }
            else {
                scheduler->stopBatchProcess();
                std::cout << "Scheduler stopped generating processes." << std::endl;
            }
        }
        else if (command == "report-util") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            }
            else {
                scheduler->printStatus(true);
                std::cout << "Report saved to csopesy-log.txt" << std::endl;
            }
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }

    return 0;
}