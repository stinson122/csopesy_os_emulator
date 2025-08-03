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
    // Ensure address is within process memory bounds and properly aligned
    if (address >= memory_size) {
        logMemoryViolation(address, "access");
        return false;
    }

    // For WRITE operations, ensure we're not writing to symbol table space
    if (address < SYMBOL_TABLE_SIZE) {
        logMemoryViolation(address, "write to protected area");
        return false;
    }

    return true;
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
    std::uniform_int_distribution<int> op_dist(0, 7);

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
        case 6: // READ
            instr.type = "READ";
            instr.operands.push_back("var" + std::to_string(i % 10));
            instr.operands.push_back("0x" + std::to_string((value_dist(gen) % (memory_size / 2)))); // Random address within bounds
            break;
        case 7: // WRITE
            instr.type = "WRITE";
            instr.operands.push_back("0x" + std::to_string((value_dist(gen) % (memory_size / 2)))); // Random address within bounds
            instr.operands.push_back(value_dist(gen)); // Random value
            break;
        }
        instructions.push_back(instr);
    }
}

// Helper function to process PRINT content and handle variable substitution
std::string Process::processPrintContent(const std::string& content) const {
    std::string result;
    size_t i = 0;
    bool in_quotes = false;

    while (i < content.length()) {
        if (content[i] == '"') {
            in_quotes = !in_quotes;
            i++;
            continue;
        }

        if (!in_quotes && content[i] == '+') {
            // Handle variable after +
            i++;
            // Skip whitespace
            while (i < content.length() && isspace(content[i])) i++;

            // Extract variable name
            size_t var_start = i;
            while (i < content.length() && (isalnum(content[i]) || content[i] == '_')) i++;
            std::string var_name = content.substr(var_start, i - var_start);

            // Get variable value
            uint16_t value = getVariableValue(var_name);
            result += std::to_string(value);
        }
        else {
            // Add character to result
            result += content[i];
            i++;
        }
    }

    return result;
}

bool Process::executeNextInstruction(int core_id, DemandPagingMemoryManager* memory_manager) {

    if (current_instruction < instructions.size()) {
        auto& instr = instructions[current_instruction];
        std::string debug_msg = "Executing: " + instr.type;
        for (auto& op : instr.operands) {
            if (std::holds_alternative<std::string>(op)) {
                debug_msg += " " + std::get<std::string>(op);
            }
            else {
                debug_msg += " " + std::to_string(std::get<uint16_t>(op));
            }
        }
        logPrint(debug_msg, core_id, std::chrono::system_clock::now());
    }

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
            try {
                std::string message = std::get<std::string>(instr.operands[0]);

                // Simple PRINT without concatenation
                if (message.find('+') == std::string::npos) {
                    // Remove surrounding quotes if present
                    if (message.size() >= 2 && message.front() == '"' && message.back() == '"') {
                        message = message.substr(1, message.size() - 2);
                    }
                    logPrint(message, core_id, std::chrono::system_clock::now());
                }
                // PRINT with concatenation
                else {
                    std::vector<std::string> parts;
                    size_t start = 0;
                    size_t end = message.find('+');

                    // Split by + operators
                    while (end != std::string::npos) {
                        std::string part = message.substr(start, end - start);
                        // Trim whitespace and quotes
                        part.erase(0, part.find_first_not_of(" \t\n\r\f\v"));
                        part.erase(part.find_last_not_of(" \t\n\r\f\v") + 1);
                        if (part.size() >= 2 && part.front() == '"' && part.back() == '"') {
                            part = part.substr(1, part.size() - 2);
                        }
                        parts.push_back(part);
                        start = end + 1;
                        end = message.find('+', start);
                    }
                    // Add last part
                    std::string last_part = message.substr(start);
                    last_part.erase(0, last_part.find_first_not_of(" \t\n\r\f\v"));
                    last_part.erase(last_part.find_last_not_of(" \t\n\r\f\v") + 1);
                    if (last_part.size() >= 2 && last_part.front() == '"' && last_part.back() == '"') {
                        last_part = last_part.substr(1, last_part.size() - 2);
                    }
                    parts.push_back(last_part);

                    // Build final message
                    std::string final_message;
                    for (const auto& part : parts) {
                        if (variables.find(part) != variables.end()) {
                            final_message += std::to_string(getVariableValue(part));
                        }
                        else {
                            final_message += part;
                        }
                    }

                    logPrint(final_message, core_id, std::chrono::system_clock::now());
                }
            }
            catch (const std::bad_variant_access&) {
                logPrint("PRINT ERROR: Invalid operand", core_id, std::chrono::system_clock::now());
            }
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
        else if (instr.type == "READ") {
            std::string var = std::get<std::string>(instr.operands[0]);
            std::string addr_str = std::get<std::string>(instr.operands[1]);
            uint64_t address = parseHexAddress(addr_str.substr(2)); // Remove "0x" prefix
            uint16_t value = 0;

            if (readMemory(address, value)) {
                declareVariable(var, value);
                // Debug output to verify successful read
                logPrint("DEBUG: Read value " + std::to_string(value) + " from address " + addr_str +
                    " into variable " + var, core_id, std::chrono::system_clock::now());
            }
            else {
                memory_violation = true;
                violation_info = "Memory read violation at address: " + addr_str;
                state = ProcessState::Crashed;
            }
        }
        else if (instr.type == "WRITE") {
            std::string addr_str = std::get<std::string>(instr.operands[0]);
            uint16_t value = getOperandValue(instr.operands[1]); // Use getOperandValue to handle variables
            uint64_t address = parseHexAddress(addr_str.substr(2)); // Remove "0x" prefix

            if (!writeMemory(address, value)) {
                memory_violation = true;
                violation_info = "Memory write violation at address: " + addr_str;
                state = ProcessState::Crashed;
            }
            else {
                // Debug output to verify successful write
                logPrint("DEBUG: Wrote value " + std::to_string(value) + " to address " + addr_str,
                    core_id, std::chrono::system_clock::now());
            }
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

void Process::parseCustomInstructions(const std::string& instruction_str) {
    // Clear existing instructions
    instructions.clear();
    remaining_instructions = 0;
    total_instructions = 0;

    std::vector<std::string> instruction_list;
    size_t start = 0;
    size_t end = instruction_str.find(';');

    // Split by semicolons
    while (end != std::string::npos) {
        instruction_list.push_back(instruction_str.substr(start, end - start));
        start = end + 1;
        end = instruction_str.find(';', start);
    }
    // Add the last instruction
    if (start < instruction_str.length()) {
        instruction_list.push_back(instruction_str.substr(start));
    }

    // Validate instruction count
    if (instruction_list.empty() || instruction_list.size() > 50) {
        throw std::runtime_error("Invalid instruction count (must be 1-50)");
    }

    // Parse each instruction
    for (auto& instr_str : instruction_list) {
        // Trim whitespace
        instr_str.erase(0, instr_str.find_first_not_of(" \t\n\r\f\v"));
        instr_str.erase(instr_str.find_last_not_of(" \t\n\r\f\v") + 1);
        if (instr_str.empty()) continue;

        Instruction instr;

        // Special handling for PRINT instructions
        if (instr_str.find("PRINT") == 0) {
            instr.type = "PRINT";
            size_t content_start = instr_str.find_first_of("(\"");

            if (content_start != std::string::npos) {
                // Find matching closing character
                char open_char = instr_str[content_start];
                char close_char = (open_char == '(') ? ')' : '"';
                size_t content_end = instr_str.find_last_of(close_char);

                if (content_end != std::string::npos && content_end > content_start) {
                    std::string content = instr_str.substr(
                        content_start + 1,
                        content_end - content_start - 1
                    );
                    // Trim whitespace
                    content.erase(0, content.find_first_not_of(" \t\n\r\f\v"));
                    content.erase(content.find_last_not_of(" \t\n\r\f\v") + 1);
                    instr.operands.push_back(content);
                }
                else {
                    // If no closing character, take everything after opening
                    std::string content = instr_str.substr(content_start + 1);
                    content.erase(0, content.find_first_not_of(" \t\n\r\f\v"));
                    instr.operands.push_back(content);
                }
            }
            else {
                // Simple PRINT without parentheses/quotes
                std::string content = instr_str.substr(5); // Skip "PRINT"
                content.erase(0, content.find_first_not_of(" \t\n\r\f\v"));
                instr.operands.push_back(content);
            }
        }
        else {
            // Regular instruction parsing
            std::istringstream iss(instr_str);
            std::string token;
            iss >> instr.type;
            std::transform(instr.type.begin(), instr.type.end(), instr.type.begin(), ::toupper);

            // Parse operands based on instruction type
            if (instr.type == "DECLARE") {
                std::string var_name;
                uint16_t value;
                if (iss >> var_name >> value) {
                    instr.operands.push_back(var_name);
                    instr.operands.push_back(value);
                }
                else {
                    throw std::runtime_error("Invalid DECLARE instruction: " + instr_str);
                }
            }
            else if (instr.type == "ADD" || instr.type == "SUBTRACT") {
                std::string dest, op1, op2;
                if (iss >> dest >> op1 >> op2) {
                    instr.operands.push_back(dest);
                    instr.operands.push_back(op1);
                    instr.operands.push_back(op2);
                }
                else {
                    throw std::runtime_error("Invalid " + instr.type + " instruction: " + instr_str);
                }
            }
            else if (instr.type == "SLEEP") {
                uint16_t ticks;
                if (iss >> ticks) {
                    instr.operands.push_back(ticks);
                }
                else {
                    throw std::runtime_error("Invalid SLEEP instruction: " + instr_str);
                }
            }
            else if (instr.type == "READ") {
                std::string var_name, addr_str;
                if (iss >> var_name >> addr_str) {
                    instr.operands.push_back(var_name);
                    instr.operands.push_back(addr_str);
                }
                else {
                    throw std::runtime_error("Invalid READ instruction: " + instr_str);
                }
            }
            else if (instr.type == "WRITE") {
                std::string addr_str;
                std::string value_str;
                if (iss >> addr_str >> value_str) {
                    instr.operands.push_back(addr_str);

                    // Check if value is a variable or number
                    try {
                        uint16_t value = std::stoul(value_str);
                        instr.operands.push_back(value);
                    }
                    catch (...) {
                        // If not a number, treat as variable name
                        instr.operands.push_back(value_str);
                    }
                }
                else {
                    throw std::runtime_error("Invalid WRITE instruction: " + instr_str);
                }
            }
            else {
                throw std::runtime_error("Unknown instruction type: " + instr.type);
            }
        }

        instructions.push_back(instr);
        total_instructions++;
        remaining_instructions++;
    }
}

uint16_t Process::getOperandValue(const Value& operand) const {
    if (std::holds_alternative<uint16_t>(operand)) {
        return std::get<uint16_t>(operand);
    }
    else if (std::holds_alternative<std::string>(operand)) {
        std::string var = std::get<std::string>(operand);
        return getVariableValue(var);
    }
    return 0;
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
    return 0; // Return 0 if variable not found
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