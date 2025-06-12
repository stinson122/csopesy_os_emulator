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

Scheduler* scheduler = nullptr;
bool initialized = false;
std::atomic<uint64_t> cpu_cycles(0);

struct Config {
    int num_cpu = 4;
    std::string scheduler_type = "fcfs";
    uint64_t quantum_cycles = 5;
    uint64_t batch_frequency = 1;
    uint64_t min_instructions = 1000;
    uint64_t max_instructions = 2000;
    uint64_t delay_per_exec = 0;
};

Config readConfig(const std::string& filename) {
    Config config;
    std::ifstream file(filename);
    std::string line;
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file" << std::endl;
        return config;
    }
    
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (line.empty()) continue;
        
        iss >> key;
        
        if (key == "num-cpu") {
            iss >> config.num_cpu;
        } else if (key == "scheduler") {
            std::string value;
            iss >> value;
            // Remove quotes if present
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            config.scheduler_type = value;
        } else if (key == "quantum-cycles") {
            iss >> config.quantum_cycles;
        } else if (key == "batch-process-freq") {
            iss >> config.batch_frequency;
        } else if (key == "min-ins") {
            iss >> config.min_instructions;
        } else if (key == "max-ins") {
            iss >> config.max_instructions;
        } else if (key == "delay-per-exec") {
            iss >> config.delay_per_exec;
        }
    }
    
    return config;
}

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
}

void drawScreen(const std::string& processName) {
    Process* p = scheduler->getProcess(processName);
    if (!p) {
        std::cout << "Process: " << processName << " (not found)" << std::endl;
    } else {
        std::cout << "Process: " << p->name << std::endl;
        int remaining = p->remaining_instructions.load();
        std::cout << "Instruction: " << (p->total_instructions - remaining) 
                  << "/" << p->total_instructions << std::endl;
    }
    std::cout << "TimeStamp: " << Scheduler::formatTimePoint(std::chrono::system_clock::now()) << std::endl;

    std::string command;
    while (true) {
        std::cout << "Enter a command (process-smi, print, exit): " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            clearScreen();
            std::cout << "Back to main menu." << std::endl;
            break;
        } else if (command == "process-smi") {
            if (p) {
                processSMI(p);
            } else {
                std::cout << "Process not found." << std::endl;
            }
        } else if (command.starts_with("print ")) {
            if (p) {
                if (p->state == ProcessState::Running) {
                    std::string message = command.substr(6);
                    auto now = std::chrono::system_clock::now();
                    p->logPrint(message, p->core_id.load(), now);
                    std::cout << "Message logged to " << p->name << ".log" << std::endl;
                } else {
                    std::cout << "Cannot print: process not running." << std::endl;
                }
            } else {
                std::cout << "Process not found." << std::endl;
            }
        } else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }
}

int main() {
    std::string command;
    printHeader();

    // Start CPU cycle counter thread
    std::thread cycle_counter([](){
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            cpu_cycles++;
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
            } else {
                Config config = readConfig("config.txt");
                scheduler = new Scheduler(config.num_cpu);
                scheduler->setMinInstructions(config.min_instructions);
                scheduler->setMaxInstructions(config.max_instructions);
                scheduler->setBatchFrequency(config.batch_frequency);
                scheduler->setDelay(config.delay_per_exec);
                initialized = true;
                std::cout << "Scheduler initialized with " << config.num_cpu << " cores." << std::endl;
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
                    if (flag == "-s") {
                        // Create new process if doesn't exist
                        if (!scheduler->getProcess(processName)) {
                            std::random_device rd;
                            std::mt19937 gen(rd());
                            std::uniform_int_distribution<uint64_t> dist(
                                scheduler->min_instructions, 
                                scheduler->max_instructions
                            );
                            uint64_t instructions = dist(gen);
                            Process* p = new Process(processName, instructions);
                            scheduler->addProcess(p);
                        }
                    }
                    
                    clearScreen();
                    std::cout << "Displaying process: " << processName << std::endl;
                    drawScreen(processName);
                }
                else {
                    std::cout << "Invalid screen command. Usage: screen -s|-r <name> or screen -ls" << std::endl;
                }
            }
        }
        else if (command == "scheduler-start") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            } else {
                scheduler->startBatchProcess();
                std::cout << "Scheduler started generating processes." << std::endl;
            }
        }
        else if (command == "scheduler-stop") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            } else {
                scheduler->stopBatchProcess();
                std::cout << "Scheduler stopped generating processes." << std::endl;
            }
        }
        else if (command == "report-util") {
            if (!initialized) {
                std::cout << "Please run 'initialize' first." << std::endl;
            } else {
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