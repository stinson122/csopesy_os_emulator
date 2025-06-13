#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <atomic>
#include <iomanip>
#include <map>
#include <filesystem>

#ifdef _WIN32
#include <cstdlib> // for system("cls")
#else
#include <unistd.h>
#endif

using namespace std::chrono;

// Global variables for the scheduler
std::mutex mtx;
std::condition_variable cv;
std::queue<std::string> processQueue;
std::vector<std::thread> cpuWorkers;
std::atomic<bool> schedulerRunning{ false };
std::atomic<bool> stopScheduler{ false };

// Process status tracking
struct ProcessStatus {
    std::string name;
    std::string status;
    int progress;
    int total;
    int core;
    std::string timestamp;
};

std::vector<ProcessStatus> runningProcesses;
std::vector<ProcessStatus> finishedProcesses;
std::mutex statusMutex;

void printHeader() {
    std::cout << R"(  .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------.  .----------------. 
| .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. || .--------------. |
| |     ______   | || |    _______   | || |     ____     | || |   ______     | || |  _________   | || |    _______   | || |  ____  ____  | |
| |   .' ___  |  | || |   /  ___  |  | || |   .'    `.   | || |  |_   __ \   | || | |_   ___  |  | || |   /  ___  |  | || | |_  _||_  _| | |
| |  / .'   \_|  | || |  |  (__ \_|  | || |  /  .--.  \  | || |    | |__) |  | || |   | |_  \_|  | || |  |  (__ \_|  | || |   \ \  / /   | |
| |  | |         | || |   '.___`-.   | || |  | |    | |  | || |    |  ___/   | || |   |  _|  _   | || |   '.___`-.   | || |    \ \/ /    | |
| |  \ `.___.'\  | || |  |`\____) |  | || |  \  `--'  /  | || |   _| |_      | || |  _| |___/ |  | || |  |`\____) |  | || |    _|  |_    | |
| |   `._____.'  | || |  |_______.'  | || |   `.____.'   | || |  |_____|     | || | |_________|  | || |  |_______.'  | || |   |______|   | |
| |              | || |              | || |              | || |              | || |              | || |              | || |              | |
| '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' || '--------------' |
 '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------'  '----------------' 
)" << std::endl;
    std::cout << "\033[32mHello, Welcome to CSOPESY command line!\033[0m" << std::endl;
    std::cout << "\033[33mType 'exit' to quit, 'clear' to clear the screen\033[0m" << std::endl;
}

void clearScreen() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
    printHeader();
}

auto getTimeStamp() {
    auto now = time_point_cast<seconds>(system_clock::now());
    auto local = zoned_time{ current_zone(), now };
    return local;
}

std::string getCurrentTimestampString() {
    auto ts = getTimeStamp();
    return std::format("{:%m/%d/%Y %I:%M:%S %p}", ts);
}

void updateProcessStatus(const std::string& name, const std::string& status, int progress, int total, int core = -1) {
    std::lock_guard<std::mutex> lock(statusMutex);

    auto it = std::find_if(runningProcesses.begin(), runningProcesses.end(),
        [&name](const ProcessStatus& ps) { return ps.name == name; });

    if (status == "Finished") {
        if (it != runningProcesses.end()) {
            ProcessStatus ps = *it;
            ps.status = status;
            ps.progress = ps.total;
            ps.timestamp = getCurrentTimestampString();
            finishedProcesses.push_back(ps);
            runningProcesses.erase(it);
        }
    }
    else {
        if (it != runningProcesses.end()) {
            it->progress = progress;
            it->core = core;
        }
        else {
            ProcessStatus ps;
            ps.name = name;
            ps.status = status;
            ps.progress = progress;
            ps.total = total;
            ps.core = core;
            ps.timestamp = getCurrentTimestampString();
            runningProcesses.push_back(ps);
        }
    }
}

void displayProcessStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);

    std::cout << "--------------------------------------\n";
    std::cout << "Running processes:\n";
    for (const auto& ps : runningProcesses) {
        std::cout << ps.name << "\t" << ps.timestamp << "\tCore: " << ps.core << "\t\t"
            << ps.progress << " / " << ps.total << std::endl;
    }

    std::cout << "\nFinished processes:\n";
    for (const auto& ps : finishedProcesses) {
        std::cout << ps.name << "\t" << ps.timestamp << "\t" << ps.status << "\t"
            << ps.progress << " / " << ps.total << std::endl;
    }
    std::cout << "--------------------------------------\n";
}

void cpuWorker(int coreId) {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [] { return !processQueue.empty() || stopScheduler; });

        if (stopScheduler && processQueue.empty()) {
            return;
        }

        if (!processQueue.empty()) {
            std::string processName = processQueue.front();
            processQueue.pop();
            lock.unlock();

            // 100 print commands
            const int totalPrintCommands = 100;
            updateProcessStatus(processName, "Running", 0, totalPrintCommands, coreId);

            // Create log file for this process
            std::filesystem::create_directory("process_logs");
            std::ofstream logFile("process_logs/" + processName + ".txt");

            logFile << "Process name: " << processName << "\n";
            logFile << "Logs: " << "\n" << "\n";

            for (int i = 1; i <= totalPrintCommands; ++i) {
                if (stopScheduler) {
                    updateProcessStatus(processName, "Stopped", i, totalPrintCommands);
                    logFile.close();
                    return;
                }

                // Simulate work (very brief delay)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                updateProcessStatus(processName, "Running", i, totalPrintCommands, coreId);

                // Execute print command
                std::string timestamp = getCurrentTimestampString();
                logFile << "(" << timestamp << ") Core:" << coreId
                    << " \"Hello world from " << processName << "!\"\n";
            }

            logFile.close();
            updateProcessStatus(processName, "Finished", totalPrintCommands, totalPrintCommands);
        }
    }
}

void generateTestProcesses() {
    for (int i = 1; i <= 10; ++i) {
        std::string processName = "process_" + std::to_string(i);

        {
            std::lock_guard<std::mutex> lock(mtx);
            processQueue.push(processName);
        }

        cv.notify_one();
    }

    //std::cout << "Generated 10 test processes, each with exactly 100 print commands.\n";
}

void startScheduler() {
    if (schedulerRunning) {
        std::cout << "Scheduler is already running.\n";
        return;
    }

    stopScheduler = false;
    schedulerRunning = true;

    // Create 4 CPU worker threads (one per core)
    for (int i = 0; i < 4; ++i) {
        cpuWorkers.emplace_back(cpuWorker, i);
    }

    std::cout << "FCFS scheduler started with 4 cores.\n";

    // Generate test processes
    generateTestProcesses();
}

void stopSchedulerCommand() {
    if (!schedulerRunning) {
        std::cout << "Scheduler is not running.\n";
        return;
    }

    stopScheduler = true;
    cv.notify_all();

    for (auto& worker : cpuWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    cpuWorkers.clear();
    schedulerRunning = false;
    std::cout << "Scheduler stopped.\n";
}

void addProcess(const std::string& processName) {
    if (!schedulerRunning) {
        std::cout << "Scheduler is not running. Start it with 'scheduler-test' first.\n";
        return;
    }

    std::lock_guard<std::mutex> lock(mtx);
    processQueue.push(processName);
    cv.notify_one();
    std::cout << "Process '" << processName << "' added to the queue.\n";
}

void drawScreen(std::string processName) {
    std::cout << "Process: " << processName << std::endl;
    std::cout << "Instruction: 1/100" << std::endl;
    std::cout << std::format("TimeStamp: {:%m/%d/%Y, %I:%M:%S %p}", getTimeStamp()) << std::endl;

    addProcess(processName);

    std::string command;
    while (command != "exit") {
        std::cout << "Type 'exit' to return to main menu" << std::endl;
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            clearScreen();
            std::cout << "Back to main menu." << std::endl;
            break;
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }
}

int main() {
    std::string command;
    printHeader();

    while (true) {
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            if (schedulerRunning) {
                stopSchedulerCommand();
            }
            std::cout << "exit command recognized. Closing program." << std::endl;
            break;
        }
        else if (command == "clear") {
            clearScreen();
        }
        else if (command == "initialize") {
            std::cout << "initialize command recognized. Doing something." << std::endl;
        }
        else if (command.starts_with("screen ")) {
            std::istringstream iss(command);
            std::string base, flag, processName;
            iss >> base >> flag >> processName;

            if ((flag == "-s" || flag == "-r") && !processName.empty()) {
                clearScreen();
                std::cout << "screen command recognized. Creating process." << std::endl;
                drawScreen(processName);
            }
            else {
                std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
            }
        }
        else if (command == "scheduler-test") {
            startScheduler();
        }
        else if (command == "scheduler-stop") {
            stopSchedulerCommand();
        }
        else if (command == "screen-ls") {
            displayProcessStatus();
        }
        else if (command == "report-util") {
            std::cout << "report-util command recognized. Doing something." << std::endl;
        }
        else {
            std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
        }
    }

    return 0;
}