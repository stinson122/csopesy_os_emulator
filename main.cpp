#include <iostream>
#include <string>
#include <chrono>

#ifdef _WIN32
#include <cstdlib> // for system("cls")
#else
#include <unistd.h>
#endif

using namespace std::chrono;

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

auto getTimeStamp() {
    auto now = time_point_cast<seconds>(system_clock::now());
    auto local = zoned_time{ current_zone(), now };
    return local;
}

void drawScreen(std::string processName) {
    std::cout << "Process: " << processName << std::endl;
    std::cout << "Instruction: 1/100" << std::endl;
    std::cout << std::format("TimeStamp: {:%m/%d/%Y, %I:%M:%S %p}", getTimeStamp()) << std::endl;

    //wait for exit
    std::string command;
    while (command != "exit") {
        std::cout << "Type 'exit' to return to main menu" << std::endl;
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
            std::cout << "Back to main menu." << std::endl;
            break;
        }
        std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
    }

}

void clearScreen() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
    printHeader();
}

int main() {
    std::string command;
    printHeader();

    while (true) {
        std::cout << "Enter a command: " << std::flush;
        std::getline(std::cin, command);

        if (command == "exit") {
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
                std::cout << "screen command recognized. Doing something." << std::endl;
                if (flag == "-s") {
                    drawScreen(processName);
                }
                else if (flag == "-r") {
                    clearScreen();
                    drawScreen(processName);
                }
            }
            else {
                std::cout << "'" << command << "' command is not recognized. Please enter a correct command." << std::endl;
            }
        }
        else if (command == "scheduler-test") {
            std::cout << "scheduler-test command recognized. Doing something." << std::endl;
        }
        else if (command == "scheduler-stop") {
            std::cout << "scheduler-stop command recognized. Doing something." << std::endl;
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
