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

    // Always declare x, y, z with initial value 0
    Instruction decl_x;
    decl_x.type = "DECLARE";
    decl_x.operands.push_back("x");
    decl_x.operands.push_back(static_cast<uint16_t>(0));
    instructions.push_back(decl_x);

    Instruction decl_y;
    decl_y.type = "DECLARE";
    decl_y.operands.push_back("y");
    decl_y.operands.push_back(static_cast<uint16_t>(0));
    instructions.push_back(decl_y);

    Instruction decl_z;
    decl_z.type = "DECLARE";
    decl_z.operands.push_back("z");
    decl_z.operands.push_back(static_cast<uint16_t>(0));
    instructions.push_back(decl_z);

    // Create FOR instruction with 100 repetitions
    Instruction for_instr;
    for_instr.type = "FOR";
    for_instr.operands.push_back(static_cast<uint16_t>(100));
    instructions.push_back(for_instr);

    // Loop body instructions
    // ADD(x, x, 1)
    Instruction add_x;
    add_x.type = "ADD";
    add_x.operands.push_back("x");
    add_x.operands.push_back("x");
    add_x.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(add_x);

    // PRINT("Value from: x = ")
    Instruction print_x;
    print_x.type = "PRINT";
    print_x.operands.push_back("Value from: x = ");
    print_x.operands.push_back("x");
    instructions.push_back(print_x);

    // ADD(y, y, 1)
    Instruction add_y;
    add_y.type = "ADD";
    add_y.operands.push_back("y");
    add_y.operands.push_back("y");
    add_y.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(add_y);

    // PRINT("Value from: y = ")
    Instruction print_y;
    print_y.type = "PRINT";
    print_y.operands.push_back("Value from: y = ");
    print_y.operands.push_back("y");
    instructions.push_back(print_y);

    // ADD(z, z, 1)
    Instruction add_z;
    add_z.type = "ADD";
    add_z.operands.push_back("z");
    add_z.operands.push_back("z");
    add_z.operands.push_back(static_cast<uint16_t>(1));
    instructions.push_back(add_z);

    // PRINT("Value from: z = ")
    Instruction print_z;
    print_z.type = "PRINT";
    print_z.operands.push_back("Value from: z = ");
    print_z.operands.push_back("z");
    instructions.push_back(print_z);

    // Pad with NOOPs to reach total_instructions (1000)
    int num_instructions_so_far = instructions.size();
    for (int i = 0; i < total_instructions - num_instructions_so_far; i++) {
        Instruction noop;
        noop.type = "NOOP";
        instructions.push_back(noop);
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
    else if (sleep_until > 0) {
        sleep_until = 0;  // Wake up if sleep time has passed
    }
	/*
	else {
        sleep_until = 0;  // Wake up if sleep time has passed
    }*/

    auto& instr = instructions[current_instruction++];

    // note: best to test with only 1 process running, use screen -s
    auto executeInstruction = [&](const Instruction& instr) {
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
            /*Temp Test Print
            std::cout << "DECLARE" << std::endl;
            std::cout << std::get<std::string>(instr.operands[0]) << "=" << std::get<std::uint16_t>(instr.operands[1]) << std::endl;
            std::cout << var << "=" << value << std::endl;*/
        }
        else if (instr.type == "NOOP") {
            // Do nothing
        }
        else if (instr.type == "ADD") {
            std::string dest = std::get<std::string>(instr.operands[0]);
            uint16_t op1 = getOperandValue(instr.operands[1]);
            uint16_t op2 = getOperandValue(instr.operands[2]);
            declareVariable(dest, op1 + op2);
            /*Temp Test Print
            std::cout << "ADD" << std::endl;
            std::cout << getOperandValue(instr.operands[1]) << "+" << getOperandValue(instr.operands[2]) << ":" << getOperandValue(instr.operands[0]) << std::endl;
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

    if (instr.type == "FOR") {
        uint16_t repeats = std::get<uint16_t>(instr.operands[0]);
        size_t loop_start = current_instruction;

        for (uint16_t i = 0; i < repeats; i++) {
            // Execute all 6 instructions in the loop body
            for (int j = 0; j < 6; j++) {
                if (loop_start + j >= instructions.size()) break;

                auto& nested_instr = instructions[loop_start + j];
                if (executeInstruction(nested_instr)) {
                    // If sleep encountered, return early
                    return false;
                }
            }
        }

        // Skip the loop body instructions
        current_instruction = loop_start + 6;
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
