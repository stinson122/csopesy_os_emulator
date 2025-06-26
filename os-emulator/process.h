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

enum class ProcessState { Waiting, Running, Finished };

using Value = std::variant<uint16_t, std::string>;

extern std::atomic<uint64_t> cpu_cycles;

class Process {
public:
    Process(const std::string& name, int total_instructions);
    ~Process();

    void logPrint(const std::string& message, int core,
        const std::chrono::system_clock::time_point& time);

    // Instruction execution
    bool executeNextInstruction(int core_id);
    void generateXYZInstructions();
    void generateRandomInstructions();

    // Variable operations
    void declareVariable(const std::string& name, uint16_t value);
    uint16_t getVariableValue(const std::string& name) const;
    uint64_t getSleepUntil() const { return sleep_until.load(); }
    bool isSleeping() const { return sleep_until > 0 && cpu_cycles < sleep_until; }

    std::string name;
    int total_instructions;
    std::atomic<int> remaining_instructions;
    std::atomic<ProcessState> state;
    std::atomic<int> core_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::function<void(const std::string&)> log_callback;

private:
    struct Instruction {
        std::string type;
        std::vector<Value> operands;
    };

    std::ofstream log_file;
    std::string log_file_name;
    std::mutex log_mutex;

    // Process memory and instructions
    std::map<std::string, uint16_t> variables;
    std::vector<Instruction> instructions;
    std::atomic<size_t> current_instruction{ 0 };
    std::atomic<uint64_t> sleep_until{ 0 };

    void openLogFile();
    uint16_t getOperandValue(const Value& operand) const;
};
#endif // PROCESS_H