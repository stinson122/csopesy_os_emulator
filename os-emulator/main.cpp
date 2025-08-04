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
    uint64_t max_overall_mem = 16384;
    uint64_t mem_per_frame = 16;
    uint64_t min_mem_per_proc = 2048;
    uint64_t max_mem_per_proc = 8192;
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

    std::string line; // Read the config file line by line

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
        else if (key == "max-overall-mem") {
            iss >> config.max_overall_mem;
        }
        else if (key == "mem-per-frame") {
            iss >> config.mem_per_frame;
        }
        else if (key == "min-mem-per-proc") {
            iss >> config.min_mem_per_proc;
        }
        else if (key == "max-mem-per-proc") {
            iss >> config.max_mem_per_proc;
        }
    }

    return config;
}

void processSMI(Process* p) {
    if (!p) return;

    std::cout << "Process name: " << p->name << std::endl;
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
                //scheduler->getMemoryManager().generateProcessSMI();
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
                scheduler = new Scheduler(config.num_cpu, config.max_overall_mem,
                    config.mem_per_frame, config.min_mem_per_proc, config.max_mem_per_proc);
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
                if ((flag == "-s" || flag == "-c" || flag == "-r") && !processName.empty()) {
                    Process* existingProcess = scheduler->getProcess(processName);

                    if (flag == "-s") {
                        // Read memory size for -s command
                        uint64_t memory_size;
                        if (!(iss >> memory_size)) {
                            std::cout << "Missing memory size parameter. Usage: screen -s <name> <memory_size>" << std::endl;
                            continue;
                        }

                        // Validate memory size
                        if (memory_size < 64 || memory_size > 65536 || (memory_size & (memory_size - 1))) {
                            std::cout << "Invalid memory allocation. Must be power of 2 between 64 and 65536 bytes." << std::endl;
                            continue;
                        }

                        Process* existingProcess = scheduler->getProcess(processName);
                        if (!existingProcess) {
                            std::random_device rd;
                            std::mt19937 gen(rd());
                            std::uniform_int_distribution<uint64_t> dist(
                                scheduler->getMinInstructions(),
                                scheduler->getMaxInstructions()
                            );
                            uint64_t instructions = dist(gen);

                            Process* p = new Process(processName, instructions, memory_size);
                            scheduler->addProcess(p);
                            std::cout << "Created new process: " << processName << " with " << memory_size << " bytes memory" << std::endl;
                        }
                        else {
                            std::cout << "Process " << processName << " already exists." << std::endl;
                            continue;
                        }
                    }
                    else if (flag == "-c") {
                        // Read memory size and instructions for -c command
                        uint64_t memory_size;
                        std::string instructions;
                        if (!(iss >> memory_size)) {
                            std::cout << "Missing memory size parameter. Usage: screen -c <name> <memory_size> \"<instructions>\"" << std::endl;
                            continue;
                        }

                        // Read the rest of the command line to get the instruction part
                        std::string instruction_part;
                        std::getline(iss, instruction_part);

                        // Trim leading whitespace
                        if (!instruction_part.empty()) {
                            size_t first_char = instruction_part.find_first_not_of(" \t\n\r\f\v");
                            if (std::string::npos != first_char) {
                                instruction_part = instruction_part.substr(first_char);
                            }
                        }

                        // Ensure instructions are properly quoted
                        if (instruction_part.length() < 2 || instruction_part.front() != '"' || instruction_part.back() != '"') {
                            std::cout << "Instructions must be enclosed in double quotes." << std::endl;
                            continue;
                        }

                        // Extract the content from between the quotes
                        instructions = instruction_part.substr(1, instruction_part.length() - 2);

                        // The shell requires inner quotes to be escaped (e.g., \").
                        size_t pos = instructions.find("\\\"");
                        while (pos != std::string::npos) {
                            instructions.replace(pos, 2, "\"");
                            pos = instructions.find("\\\"", pos + 1);
                        }

                        // Validate memory size
                        if (memory_size < 64 || memory_size > 65536 || (memory_size & (memory_size - 1))) {
                            std::cout << "Invalid memory allocation. Must be power of 2 between 64 and 65536 bytes." << std::endl;
                            continue;
                        }

                        Process* existingProcess = scheduler->getProcess(processName);
                        if (!existingProcess) {
                            try {
                                Process* p = new Process(processName, 0, memory_size); // 0 instructions initially
                                p->parseCustomInstructions(instructions); // This will set the actual instructions
                                scheduler->addProcess(p);
                                std::cout << "Created new process: " << processName << " with " << memory_size
                                    << " bytes memory and custom instructions" << std::endl;
                            }
                            catch (const std::exception& e) {
                                std::cout << "Error creating process: " << e.what() << std::endl;
                                continue;
                            }
                        }
                        else {
                            std::cout << "Process " << processName << " already exists." << std::endl;
                            continue;
                        }

                        clearScreen();
                        std::cout << "Displaying process: " << processName << std::endl;
                        drawScreen(processName);
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
                    }
                    else if (flag == "-r") {
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
        else if (command == "process-smi") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            } else {
                scheduler->getMemoryManager().generateProcessSMI();
            }
        }
        else if (command == "vmstat") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            } else {
                scheduler->getMemoryManager().generateVMStat();
            }
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }

    return 0;
}