#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <chrono>
#include <atomic>
#include <fstream>
#include <mutex>
#include <vector>
#include <map>
#include <variant>
#include <functional>
#include <sstream>

enum class ProcessState { Waiting, Running, Finished, Crashed };

using Value = std::variant<uint16_t, std::string, uint64_t>; // Added uint64_t for memory addresses

extern std::atomic<uint64_t> cpu_cycles;
extern std::atomic<uint64_t> quantum_counter;

// Forward declaration
class DemandPagingMemoryManager;

class Process {
public:
    Process(const std::string& name, int total_instructions, uint64_t memory_size = 0);
    //Process(const std::string& name, const std::string& custom_instructions, uint64_t memory_size);
    //~Process();

    void logPrint(const std::string& message, int core,
        const std::chrono::system_clock::time_point& time);
    std::vector<std::string> getLogMessages();

    // Instruction execution
    bool executeNextInstruction(int core_id, DemandPagingMemoryManager* memory_manager = nullptr);
    void generateRandomInstructions();
    void parseCustomInstructions(const std::string& instructions_str);

    // Variable operations
    void declareVariable(const std::string& name, uint16_t value);
    uint16_t getVariableValue(const std::string& name) const;
    uint64_t getSleepUntil() const { return sleep_until.load(); }
    bool isSleeping() const { return sleep_until > 0 && cpu_cycles < sleep_until; }

    // Memory operations
    void setMemoryManager(DemandPagingMemoryManager* manager) { memory_manager = manager; }
    uint64_t getMemorySize() const { return memory_size; }
    bool hasMemoryViolation() const { return memory_violation; }
    std::string getMemoryViolationInfo() const { return violation_info; }
    std::atomic<bool> memory_wait_logged{ false }; // Add this line

    std::string name;
    int total_instructions;
    std::atomic<int> remaining_instructions;
    std::atomic<ProcessState> state;
    std::atomic<int> core_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::system_clock::time_point crash_time;
    std::function<void(const std::string&)> log_callback;
    uint64_t memory_start = 0;
    uint64_t memory_end = 0;

private:
    struct Instruction {
        std::string type;
        std::vector<Value> operands;
    };

    std::vector<std::string> log_messages;
    std::mutex log_mutex;

    // Process memory and instructions
    std::map<std::string, uint16_t> variables;
    std::vector<Instruction> instructions;
    std::atomic<size_t> current_instruction{ 0 };
    std::atomic<uint64_t> sleep_until{ 0 };

    // Memory management
    uint64_t memory_size;
    DemandPagingMemoryManager* memory_manager = nullptr;
    bool memory_violation = false;
    std::string violation_info;
    static const uint64_t SYMBOL_TABLE_SIZE = 64; // 64 bytes for symbol table
    uint64_t symbol_table_used = 0; // Track used bytes in symbol table

    uint16_t getOperandValue(const Value& operand) const;
    uint64_t parseHexAddress(const std::string& hex_str) const;
    bool isValidMemoryAccess(uint64_t address);
    void logMemoryViolation(uint64_t address, const std::string& operation);
    bool readMemory(uint64_t address, uint16_t& value);
    bool writeMemory(uint64_t address, uint16_t value);
};

#endif // PROCESS_H