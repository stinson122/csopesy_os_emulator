#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <list>
#include <vector>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <random>

struct MemoryBlock {
    uint64_t start;
    uint64_t end;
    Process* process;
    bool allocated;
};

class MemoryManager {
public:
    MemoryManager(uint64_t total_mem, uint64_t frame_size, uint64_t proc_mem)
        : total_memory(total_mem), frame_size(frame_size), proc_memory(proc_mem) {
        memory_blocks.push_back({ 0, total_mem - 1, nullptr, false });
    }

    bool allocateFirstFit(Process* p);
    void deallocate(Process* p);
    void generateMemorySnapshot(uint64_t quantum, const std::string& timestamp);

private:
    std::list<MemoryBlock> memory_blocks;
    std::mutex mem_mutex;
    uint64_t total_memory;
    uint64_t frame_size;
    uint64_t proc_memory;
};

class Scheduler {
public:
    Scheduler(int num_cores, uint64_t total_mem, uint64_t frame_size, uint64_t proc_mem);
    ~Scheduler();

    void start();
    void stop();
    void addProcess(Process* process);
    Process* getProcess(const std::string& name);
    int getActiveCores();
    int getQueueSize();
    void printStatus(bool toFile = false);
    std::vector<uint64_t> quantum_counters;

    static std::string formatTimePoint(const std::chrono::system_clock::time_point& tp);

    void setSchedulerType(const std::string& type) { scheduler_type = type; }
    void setQuantumCycles(uint64_t quantum) { quantum_cycles = quantum; }
    void setMinInstructions(uint64_t min) { min_instructions = min; }
    void setMaxInstructions(uint64_t max) { max_instructions = max; }
    void setBatchFrequency(uint64_t freq) { batch_frequency = freq; }
    void setDelay(uint64_t delay) { delay_per_exec = delay; }

    uint64_t getQuantumCycles() const { return quantum_cycles; }
    uint64_t getMinInstructions() const { return min_instructions; }
    uint64_t getMaxInstructions() const { return max_instructions; }

    void startBatchProcess();
    void stopBatchProcess();
    bool isBatchRunning() const { return batch_running; }

private:
    int num_cores;
    std::vector<Process*> cores;
    std::queue<Process*> process_queue;
    std::list<Process*> finished_processes;
    std::mutex queue_mutex;
    std::mutex cores_mutex;
    std::mutex finished_mutex;
    std::mutex all_processes_mutex;
    std::map<std::string, Process*> all_processes;

    MemoryManager memory_manager;
    std::atomic<uint64_t> current_quantum{ 0 };

    std::thread scheduler_thread;
    std::vector<std::thread> workers;
    std::atomic<bool> stop_requested;
    bool is_running;

    // Batch processing
    std::thread batch_thread;
    std::atomic<bool> batch_running{ false };
    std::atomic<bool> stop_batch{ false };
    std::string scheduler_type = "fcfs";
    uint64_t quantum_cycles = 5;
    uint64_t batch_frequency = 1;
    uint64_t min_instructions = 1;
    uint64_t max_instructions = 2000;
    uint64_t delay_per_exec = 100;
    std::atomic<int> process_counter{ 1 };

    void schedule();
    void worker(int core_id);
    void batchWorker();
};

#endif // SCHEDULER_H