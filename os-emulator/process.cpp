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
    state(ProcessState::Waiting), core_id(-1)
    //start_time(std::chrono::system_clock::now()),
    //log_file_name(name + ".log") 
{
    quantum_counter = 0;
    generateRandomInstructions();
}

void Process::generateRandomInstructions() {
    instructions.clear();

    // Declare x with initial value 0
    Instruction decl_x;
    decl_x.type = "DECLARE";
    decl_x.operands.push_back("x");
    decl_x.operands.push_back(static_cast<uint16_t>(0));
    instructions.push_back(decl_x);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist(1, 10);

    // Generate alternating PRINT and ADD instructions
    for (int i = 1; i < total_instructions; i++) {
        if (i % 2 == 1) {
            // PRINT instruction
            Instruction print_instr;
            print_instr.type = "PRINT";
            print_instr.operands.push_back("Value from: x = ");
            print_instr.operands.push_back("x");
            instructions.push_back(print_instr);
        }
        else {
            // ADD instruction
            Instruction add_instr;
            add_instr.type = "ADD";
            add_instr.operands.push_back("x");
            add_instr.operands.push_back("x");
            add_instr.operands.push_back(dist(gen));
            instructions.push_back(add_instr);
        }
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
        return false; // Still sleeping
    }
    else if (sleep_until > 0) {
        sleep_until = 0; // Wake up if sleep time has passed
    }

    auto& instr = instructions[current_instruction]; // Get current instruction

    bool instruction_executed = false; // Flag to indicate if a "real" instruction was executed

    if (instr.type == "FOR") {
        uint16_t repeats = std::get<uint16_t>(instr.operands[0]);
        size_t loop_start_index = current_instruction + 1; // Assuming loop body follows immediately

        for (uint16_t i = 0; i < repeats; ++i) {
            // Execute the 6 instructions of the loop body
            for (int j = 0; j < 6; ++j) {
                if (loop_start_index + j >= instructions.size()) {
                    break; // Prevent out-of-bounds access
                }
                auto& nested_instr = instructions[loop_start_index + j];
                // Execute the instruction, if it's SLEEP, handle it.
                // If logPrint is called within this, logs will appear.
                if (nested_instr.type == "PRINT") {
                    std::string message;
                    for (const auto& op : nested_instr.operands) {
                        if (std::holds_alternative<std::string>(op)) {
                            std::string str = std::get<std::string>(op);
                            auto it = variables.find(str);
                            if (it != variables.end()) {
                                message += std::to_string(it->second);
                            }
                            else {
                                message += str;
                            }
                        }
                        else if (std::holds_alternative<uint16_t>(op)) {
                            message += std::to_string(std::get<uint16_t>(op));
                        }
                    }
                    logPrint(message, core_id, std::chrono::system_clock::now());
                }
                else if (nested_instr.type == "DECLARE") {
                    std::string var = std::get<std::string>(nested_instr.operands[0]);
                    uint16_t value = std::get<uint16_t>(nested_instr.operands[1]);
                    declareVariable(var, value);
                }
                else if (nested_instr.type == "ADD") {
                    std::string dest = std::get<std::string>(nested_instr.operands[0]);
                    uint16_t op1 = getOperandValue(nested_instr.operands[1]);
                    uint16_t op2 = getOperandValue(nested_instr.operands[2]);
                    declareVariable(dest, op1 + op2);
                }
                else if (nested_instr.type == "SUBTRACT") {
                    std::string dest = std::get<std::string>(nested_instr.operands[0]);
                    uint16_t op1 = getOperandValue(nested_instr.operands[1]);
                    uint16_t op2 = getOperandValue(nested_instr.operands[2]);
                    declareVariable(dest, std::max(0, static_cast<int>(op1 - op2)));
                }
                else if (nested_instr.type == "SLEEP") {
                    uint8_t ticks = static_cast<uint8_t>(std::get<uint16_t>(nested_instr.operands[0]));
                    sleep_until = cpu_cycles + ticks;
                    
                    current_instruction = loop_start_index + j; // Point to the SLEEP instruction
                    remaining_instructions--; // Count the SLEEP instruction itself
                    return false; // Process is now sleeping, scheduler should re-queue later
                }
            }
        }
        // After the FOR loop finishes all its repetitions
        current_instruction += 7; // Skip the FOR instruction itself and the 6 instructions in its body
        remaining_instructions -= (repeats * 6); // Total instructions consumed by FOR
        instruction_executed = true; // FOR loop effectively executed
    }
    else {
        // Execute non-FOR instructions (PRINT, DECLARE, ADD, SUBTRACT, NOOP, SLEEP)
        // This is the common path for single instructions.
        if (instr.type == "PRINT") {
            std::string message;
            for (const auto& op : instr.operands) {
                if (std::holds_alternative<std::string>(op)) {
                    std::string str = std::get<std::string>(op);
                    auto it = variables.find(str);
                    if (it != variables.end()) {
                        message += std::to_string(it->second);
                    }
                    else {
                        message += str;
                    }
                }
                else if (std::holds_alternative<uint16_t>(op)) {
                    message += std::to_string(std::get<uint16_t>(op));
                }
            }
            logPrint(message, core_id, std::chrono::system_clock::now());
        }
        else if (instr.type == "DECLARE") {
            std::string var = std::get<std::string>(instr.operands[0]);
            uint16_t value = std::get<uint16_t>(instr.operands[1]);
            declareVariable(var, value);
        }
        else if (instr.type == "NOOP") {
            // Do nothing
        }
        else if (instr.type == "ADD") {
            std::string dest = std::get<std::string>(instr.operands[0]);
            uint16_t op1 = getOperandValue(instr.operands[1]);
            uint16_t op2 = getOperandValue(instr.operands[2]);
            declareVariable(dest, op1 + op2);
        }
        else if (instr.type == "SUBTRACT") {
            std::string dest = std::get<std::string>(instr.operands[0]);
            uint16_t op1 = getOperandValue(instr.operands[1]);
            uint16_t op2 = getOperandValue(instr.operands[2]);
            declareVariable(dest, std::max(0, static_cast<int>(op1 - op2)));
        }
        else if (instr.type == "SLEEP") {
            uint8_t ticks = static_cast<uint8_t>(std::get<uint16_t>(instr.operands[0]));
            sleep_until = cpu_cycles + ticks;
            remaining_instructions--; // The SLEEP instruction itself is 'executed'
            current_instruction++; // Move past the SLEEP instruction
            return false; // Indicate that the process is now sleeping
        }
        instruction_executed = true; // A non-FOR instruction was executed
        current_instruction++; // Move to the next instruction
    }

    if (instruction_executed) {
        remaining_instructions--;
    }

    if (remaining_instructions.load() <= 0) {
        state = ProcessState::Finished;
        end_time = std::chrono::system_clock::now();
        return true; // Process finished
    }
    return false; // Process is still running or went to sleep
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
/*
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
*/
void Process::logPrint(const std::string& message, int core,
    const std::chrono::system_clock::time_point& time)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    auto zt = std::chrono::zoned_time{ std::chrono::current_zone(),
        std::chrono::time_point_cast<std::chrono::seconds>(time) };
    std::string log_line = "(" + std::format("{:%m/%d/%Y %I:%M:%S%p}", zt) +
        ") Core:" + std::to_string(core) + " \"" + message + "\"\n";

    // Store in vector instead of writing to file
    log_messages.push_back(log_line);

    // Preserve callback functionality
    if (log_callback) {
        log_callback(log_line);
    }
}

std::vector<std::string> Process::getLogMessages() {
	std::lock_guard<std::mutex> lock(log_mutex);
	return log_messages;
}
