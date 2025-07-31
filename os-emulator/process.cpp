#include "process.h"
#include "memory_manager.h"
#include <cstdint>
#include <iomanip>
#include <chrono>
#include <format>
#include <random>
#include <algorithm>
#include <iostream>

// Helper function to convert string to unsigned long
uint64_t Process::parseHexAddress(const std::string& hex_str) const {
    try {
        return std::stoul(hex_str, nullptr, 16);
    }
    catch (...) {
        return 0;
    }
}

// Check if a memory address is valid for this process
bool Process::isValidMemoryAccess(uint64_t address) {
    return address < memory_size;
}

// Log memory violation details
void Process::logMemoryViolation(uint64_t address, const std::string& operation) {
    memory_violation = true;
    violation_info = "Memory violation at " + operation + " address: 0x" +
        std::to_string(address) + " in process: " + name;
}

// Read from memory using memory manager
bool Process::readMemory(uint64_t address, uint16_t& value) {
    if (!memory_manager) return false;
    if (!isValidMemoryAccess(address)) {
        logMemoryViolation(address, "read");
        return false;
    }
    return memory_manager->readMemory(this, address, value);
}

// Write to memory using memory manager
bool Process::writeMemory(uint64_t address, uint16_t value) {
    if (!memory_manager) return false;
    if (!isValidMemoryAccess(address)) {
        logMemoryViolation(address, "write");
        return false;
    }
    return memory_manager->writeMemory(this, address, value);
}

Process::Process(const std::string& name, int total_instructions, uint64_t memory_size)
    : name(name), total_instructions(total_instructions),
    remaining_instructions(total_instructions),
    state(ProcessState::Waiting), core_id(-1),
    memory_size(memory_size)  // Initialize memory_size
{
    generateRandomInstructions();
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
            instr.operands.push_back(value_dist(gen)); // Already uint16_t
            break;
        case 2: // ADD
            instr.type = "ADD";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back("var" + std::to_string((i + 1) % 10));
            instr.operands.push_back(value_dist(gen)); // Already uint16_t
            break;
        case 3: // SUBTRACT
            instr.type = "SUBTRACT";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back("var" + std::to_string((i + 1) % 10));
            instr.operands.push_back(value_dist(gen)); // Already uint16_t
            break;
        case 4: // SLEEP
            instr.type = "SLEEP";
            // Cast to uint16_t to match variant type
            instr.operands.push_back(static_cast<uint16_t>(
                static_cast<uint8_t>(value_dist_uint8(gen) % 10 + 1)
                ));
            break;
        case 5: // FOR
            instr.type = "FOR";
            instr.operands.push_back(static_cast<uint16_t>(3)); // Repeat count
            break;
        }
        instructions.push_back(instr);
    }
}

bool Process::executeNextInstruction(int core_id, DemandPagingMemoryManager* memory_manager) {
    if (memory_manager) {
        this->memory_manager = memory_manager;
    }
    
    if (current_instruction >= instructions.size()) {
        state = ProcessState::Finished;
        end_time = std::chrono::system_clock::now();
        return true;
    }

    if (sleep_until > 0 && cpu_cycles < sleep_until) {
        remaining_instructions--;
        return false;
    }
    else if (sleep_until > 0) {
        sleep_until = 0;
    }

    auto& instr = instructions[current_instruction++];
    bool sleep_triggered = false;

    auto executeInstruction = [&](const Instruction& instr) {
        if (instr.type == "PRINT") {
            std::string message = std::get<std::string>(instr.operands[0]);
            logPrint(message, core_id, std::chrono::system_clock::now());
        }
        else if (instr.type == "DECLARE") {
            std::string var = std::get<std::string>(instr.operands[0]);
            uint16_t value = std::get<uint16_t>(instr.operands[1]);
            declareVariable(var, value);
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
            sleep_triggered = true;
        }
        return sleep_triggered;
        };

    try {
        if (executeInstruction(instr)) {
            return false;
        }

        if (instr.type == "FOR") {
            uint16_t repeats = std::get<uint16_t>(instr.operands[0]);
            size_t loop_start = current_instruction;
            for (uint16_t i = 0; i < repeats; i++) {
                current_instruction = loop_start;
                if (current_instruction >= instructions.size()) break;

                auto& nested_instr = instructions[current_instruction++];
                if (executeInstruction(nested_instr)) {
                    break;
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
    return getVariableValue(var);
}

void Process::declareVariable(const std::string& name, uint16_t value) {
    // Calculate address in symbol table
    uint64_t address = symbol_table_used;

    // Check if we have space in symbol table
    if (address + sizeof(uint16_t) > SYMBOL_TABLE_SIZE) {
        memory_violation = true;
        violation_info = "Symbol table full for variable: " + name;
        return;
    }

    // Store variable in memory
    if (!writeMemory(address, value)) {
        memory_violation = true;
        violation_info = "Failed to declare variable: " + name;
        return;
    }

    // Update symbol table usage
    symbol_table_used += sizeof(uint16_t);
    variables[name] = value;  // Keep local cache for faster access
}

uint16_t Process::getVariableValue(const std::string& name) const {
    auto it = variables.find(name);
    if (it != variables.end()) {
        return it->second;
    }
    return 0;
}

void Process::logPrint(const std::string& message, int core,
    const std::chrono::system_clock::time_point& time)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    auto zt = std::chrono::zoned_time{ std::chrono::current_zone(),
        std::chrono::time_point_cast<std::chrono::seconds>(time) };
    std::string log_line = "(" + std::format("{:%m/%d/%Y %I:%M:%S%p}", zt) +
        ") Core:" + std::to_string(core) + " \"" + message + "\"\n";

    log_messages.push_back(log_line);

    if (log_callback) {
        log_callback(log_line);
    }
}

std::vector<std::string> Process::getLogMessages() {
    std::lock_guard<std::mutex> lock(log_mutex);
    return log_messages;
}