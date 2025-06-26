#include "process.h"
#include <cstdint>
#include <iomanip>
#include <chrono>
#include <format>
#include <random>
#include <algorithm>
#include <iostream>

Process::Process(const std::string& name, int total_instructions)
    : name(name), total_instructions(total_instructions),
    remaining_instructions(total_instructions),
    state(ProcessState::Waiting), core_id(-1),
    //start_time(std::chrono::system_clock::now()),
    log_file_name(name + ".log") {
    //generateRandomInstructions();

    declareVariable("x", 0);
    declareVariable("y", 0);
    declareVariable("z", 0);
    generateXYZInstructions();
}

void Process::generateXYZInstructions() {
    Instruction forLoop;
    forLoop.type = "FOR";
    forLoop.operands.push_back(static_cast<uint16_t>(100));
    instructions.push_back(forLoop);

    // Create the body of the FOR loop
    // ADD (x, x, 1)
    Instruction addX;
    addX.type = "ADD";
    addX.operands.push_back("x");
    addX.operands.push_back("x");
    addX.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(addX);

    // PRINT("Value from: " + x)
    Instruction printX;
    printX.type = "PRINT";
    printX.operands.push_back("Hello from: ");
    printX.operands.push_back("x");

    instructions.push_back(printX);

    // ADD (y, y, 1)
    Instruction addY;
    addY.type = "ADD";
    addY.operands.push_back("y");
    addY.operands.push_back("y");
    addY.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(addY);

    // PRINT("Value from: " + y)
    Instruction printY;
    printY.type = "PRINT";
    printY.operands.push_back("Hello from: ");
    printY.operands.push_back("y");

    instructions.push_back(printY);

    // ADD (z, z, 1)
    Instruction addZ;
    addZ.type = "ADD";
    addZ.operands.push_back("z");
    addZ.operands.push_back("z");
    addZ.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(addZ);

    // PRINT("Value from: " + z)
    Instruction printZ;
    printZ.type = "PRINT";
    printZ.operands.push_back("Hello from: ");
    printZ.operands.push_back("z");

    instructions.push_back(printZ);
}

void Process::generateRandomInstructions() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> value_dist(0, 100);
    std::uniform_int_distribution<uint16_t> value_dist_uint8(0, 100);
    std::uniform_int_distribution<int> op_dist(0, 5);

    for (int i = 0; i < total_instructions; i++) {
        Instruction instr;
        switch (op_dist(gen)) {
        case 0: // PRINT
            instr.type = "PRINT";
            instr.operands.push_back("Hello world from " + name + "!");
            break;
        case 1: // DECLARE
            instr.type = "DECLARE";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back(value_dist(gen));
            break;
        case 2: // ADD
            instr.type = "ADD";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back("var" + std::to_string((i + 1) % 10));
            instr.operands.push_back(value_dist(gen));
            break;
        case 3: // SUBTRACT
            instr.type = "SUBTRACT";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back("var" + std::to_string((i + 1) % 10));
            instr.operands.push_back(value_dist(gen));
            break;
        case 4: // SLEEP
            instr.type = "SLEEP";
            instr.operands.push_back(static_cast<uint8_t>(value_dist_uint8(gen) % 10 + 1));
            break;
        case 5: // FOR
            instr.type = "FOR";
            instr.operands.push_back(static_cast<uint16_t>(3)); // Repeat count
            // The next instruction will be treated as the loop body
            break;
        }
        instructions.push_back(instr);
    }
}

bool Process::executeNextInstruction(int core_id) {
    if (current_instruction >= instructions.size()) {
        state = ProcessState::Finished;
        end_time = std::chrono::system_clock::now();
        return true;
    }

    // Check if process is sleeping
    if (sleep_until > 0 && cpu_cycles < sleep_until) {
        remaining_instructions--;  // Count sleep as an instruction
        return false;
    }
    else {
        sleep_until = 0;  // Wake up if sleep time has passed
    }

    auto& instr = instructions[current_instruction++];

    // remove temp test prints before final
    // note: best to test with only 1 process running, use screen -s
    auto executeInstruction = [&](const Instruction& instr, uint16_t loop_count = 0) {
        if (instr.type == "PRINT") {
            uint16_t value = getVariableValue(std::get<std::string>(instr.operands[1]));
            std::string message = std::get<std::string>(instr.operands[0]) + std::to_string(value);
            if (loop_count > 0) { //remove number printed at end before final
                logPrint(message + " " + std::to_string(loop_count),
                    core_id, std::chrono::system_clock::now());
            } else {
                logPrint(message, core_id, std::chrono::system_clock::now());
            }
        }
        else if (instr.type == "DECLARE") {
            std::string var = std::get<std::string>(instr.operands[0]);
            uint16_t value = std::get<uint16_t>(instr.operands[1]);
            declareVariable(var, value);
            /*Temp Test Print
            std::cout << "DECLARE" << std::endl;
            std::cout << std::get<std::string>(instr.operands[0]) << "=" << std::get<std::uint16_t>(instr.operands[1]) << std::endl;
            std::cout << var << "=" << value << std::endl;*/
        }
        else if (instr.type == "ADD") {
            std::string dest = std::get<std::string>(instr.operands[0]);
            uint16_t op1 = getOperandValue(instr.operands[1]);
            uint16_t op2 = getOperandValue(instr.operands[2]);
            declareVariable(dest, op1 + op2);
            /*Temp Test Print
            std::cout << "ADD" << std::endl;
            std::cout << getOperandValue(instr.operands[1]) << "+" << getOperandValue(instr.operands[2]) << "=" << getOperandValue(instr.operands[0]) << std::endl;
            std::cout << op1 << "+" << op2 << "=" << getOperandValue(instr.operands[0]) << std::endl;*/
        }
        else if (instr.type == "SUBTRACT") {
            std::string dest = std::get<std::string>(instr.operands[0]);
            uint16_t op1 = getOperandValue(instr.operands[1]);
            uint16_t op2 = getOperandValue(instr.operands[2]);
            declareVariable(dest, std::max(0, static_cast<int>(op1 - op2)));
            /*Temp Test Print
            std::cout << "SUB" << std::endl;
            std::cout << getOperandValue(instr.operands[1]) << "-" << getOperandValue(instr.operands[2]) << ":" << getOperandValue(instr.operands[0]) << std::endl;
            std::cout << op1 << "-" << op2 << "=" << getOperandValue(instr.operands[0]) << std::endl;*/
        }
        else if (instr.type == "SLEEP") {
            uint8_t ticks = static_cast<uint8_t>(std::get<uint16_t>(instr.operands[0]));
            sleep_until = cpu_cycles + ticks;
            /*Temp Test Print
            std::cout << "SLEEP for " << static_cast<int>(ticks) << std::endl;
            return true; //true for sleep */
        }
            return false;
        };

    try {
        if (executeInstruction(instr)) {
            return false; // stop if sleep
        }

        if (instr.type == "FOR") {
            uint16_t repeats = std::get<uint16_t>(instr.operands[0]);
            size_t loop_start = current_instruction;
            
            for (uint16_t i = 0; i < repeats; i++) {
                current_instruction = loop_start;
                // Execute all 6 instructions
                for (size_t j = 0; j < 6; j++) {
                    if (current_instruction >= instructions.size()) break;
                    auto& nested_instr = instructions[current_instruction];
                    
                    if (nested_instr.type == "ADD") {
                        std::string dest = std::get<std::string>(nested_instr.operands[0]);
                        uint16_t op1 = getOperandValue(nested_instr.operands[1]);
                        uint16_t op2 = getOperandValue(nested_instr.operands[2]);
                        declareVariable(dest, op1 + op2);
                    }
                    else if (nested_instr.type == "PRINT") {
                        std::string var = std::get<std::string>(nested_instr.operands[1]);
                        uint16_t value = getVariableValue(var);
                        std::string message = std::get<std::string>(nested_instr.operands[0]) + std::to_string(value);
                        logPrint(message + " " + std::to_string(i + 1), core_id, std::chrono::system_clock::now());
                    }
                    current_instruction++;
                    remaining_instructions--;
                }
            }
        }
    }
    catch (const std::bad_variant_access& e) {
        std::cerr << "Error executing instruction: " << e.what() << std::endl;
    }

    remaining_instructions--;
    return false;
}

uint16_t Process::getOperandValue(const Value& operand) const {
    if (std::holds_alternative<uint16_t>(operand)) {
        return std::get<uint16_t>(operand);
    }
    std::string var = std::get<std::string>(operand);
    auto it = variables.find(var);
    return it != variables.end() ? it->second : 0;
}

void Process::declareVariable(const std::string& name, uint16_t value) {
    variables[name] = std::min(value, static_cast<uint16_t>(65535));
    /*Temp Test Print
    std::cout << "Variable declared to: " << name << "=" << value << std::endl;*/
}

uint16_t Process::getVariableValue(const std::string& name) const {
    auto it = variables.find(name);
    return it != variables.end() ? it->second : 0;
}

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
    std::string log_line = "(" + std::format("{:%m/%d/%Y %I:%M:%S%p}", zt) +
        ") Core:" + std::to_string(core) + " \"" + message + "\"\n";

    log_file << log_line;
    log_file.flush();
    if (log_callback) {
        log_callback(log_line);
    }
}