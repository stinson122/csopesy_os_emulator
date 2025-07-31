#include "scheduler.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <random>

Scheduler::Scheduler(int num_cores, uint64_t total_mem, uint64_t frame_size,
    uint64_t min_mem_per_proc, uint64_t max_mem_per_proc)
    : num_cores(num_cores), cores(num_cores, nullptr),
    stop_requested(false), is_running(false),
    memory_manager(total_mem, frame_size, min_mem_per_proc, max_mem_per_proc),
    min_mem_per_proc(min_mem_per_proc),
    max_mem_per_proc(max_mem_per_proc)
{}

Scheduler::~Scheduler() {
    stop();
    stopBatchProcess();
    // Cleanup processes
    for (auto& p : all_processes) {
        delete p.second;
    }
}

void Scheduler::start() {
    if (is_running) return;
    stop_requested = false;
    is_running = true;
    quantum_counters.resize(num_cores, 0);
    scheduler_thread = std::thread(&Scheduler::schedule, this);
    for (int i = 0; i < num_cores; i++) {
        workers.push_back(std::thread(&Scheduler::worker, this, i));
    }
}

void Scheduler::stop() {
    if (!is_running) return;
    stop_requested = true;
    if (scheduler_thread.joinable()) {
        scheduler_thread.join();
    }
    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers.clear();
    is_running = false;
}

void Scheduler::addProcess(Process* process) {
    {
        std::lock_guard<std::mutex> lock(all_processes_mutex);
        all_processes[process->name] = process;
    }
    std::lock_guard<std::mutex> lock(queue_mutex);
    process_queue.push(process);
}

Process* Scheduler::getProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(all_processes_mutex);
    auto it = all_processes.find(name);
    if (it != all_processes.end()) {
        return it->second;
    }
    return nullptr;
}

int Scheduler::getActiveCores() {
    std::lock_guard<std::mutex> lock(cores_mutex);
    int count = 0;
    for (int i = 0; i < num_cores; i++) {
        if (cores[i] != nullptr && cores[i]->state == ProcessState::Running) {
            count++;
        }
    }
    return count;
}

int Scheduler::getQueueSize() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return process_queue.size();
}

std::string Scheduler::formatTimePoint(const std::chrono::system_clock::time_point& tp) {
    auto zt = std::chrono::zoned_time{ std::chrono::current_zone(),
        std::chrono::time_point_cast<std::chrono::seconds>(tp) };
    return std::format("{:%m/%d/%Y %I:%M:%S%p}", zt);
}

void Scheduler::printStatus(bool toFile) {
    std::ostream* out;
    std::ofstream file_out;

    if (toFile) {
        file_out.open("csopesy-log.txt");
        out = &file_out;
    }
    else {
        out = &std::cout;
    }

    int active_cores = getActiveCores();
    float utilization = (static_cast<float>(active_cores) / num_cores) * 100.0f;

    *out << "--------------------------------------" << std::endl;
    *out << "CPU Utilization: " << std::fixed << std::setprecision(0) << utilization << "%" << std::endl;
    *out << "Active Cores: " << getActiveCores() << std::endl;
    *out << "Cores Available: " << (num_cores - getActiveCores()) << std::endl;
    *out << "Processes in queue: " << getQueueSize() << std::endl;

    // Add memory information
    *out << "Memory Usage: " << memory_manager.getUsedMemory() << " / "
        << memory_manager.getTotalMemory() << " bytes" << std::endl;
    *out << "Memory Utilization: " << std::fixed << std::setprecision(1)
        << (static_cast<double>(memory_manager.getUsedMemory()) / memory_manager.getTotalMemory() * 100.0) << "%" << std::endl;

    *out << "--------------------------------------" << std::endl;
    *out << "Running processes:" << std::endl;

    {
        std::lock_guard<std::mutex> lock(cores_mutex);
        for (int i = 0; i < num_cores; i++) {
            if (cores[i]) {
                Process* p = cores[i];
                int done = p->total_instructions - p->remaining_instructions.load();
                *out << p->name << "     ("
                    << formatTimePoint(p->start_time)
                    << ")     Core: " << i << "     "
                    << done << " / " << p->total_instructions
                    << "     Memory: " << p->getMemorySize() << " bytes" << std::endl;
            }
        }
    }

    *out << "\nFinished processes:" << std::endl;
    std::lock_guard<std::mutex> lock(finished_mutex);
    for (Process* p : finished_processes) {
        *out << p->name << "     ("
            << formatTimePoint(p->end_time)
            << ")     Finished     "
            << p->total_instructions << " / " << p->total_instructions << std::endl;
    }
    *out << "--------------------------------------" << std::endl;

    if (toFile) {
        file_out.close();
    }
}

void Scheduler::startBatchProcess() {
    if (batch_running) return;
    stop_batch = false;
    batch_running = true;
    batch_thread = std::thread(&Scheduler::batchWorker, this);
}

void Scheduler::stopBatchProcess() {
    if (!batch_running) return;
    stop_batch = true;
    if (batch_thread.joinable()) {
        batch_thread.join();
    }
    batch_running = false;
}

void Scheduler::batchWorker() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(min_instructions, max_instructions);
    std::uniform_int_distribution<uint64_t> mem_dist(min_mem_per_proc, max_mem_per_proc);

    while (!stop_batch) {
        std::string name = "p" + std::to_string(process_counter++);
        uint64_t instructions = dist(gen);
        uint64_t memory_size = mem_dist(gen);

        // Round to nearest multiple of frame size
        uint64_t frame_size = memory_manager.getFrameSize();
        uint64_t pages = (memory_size + frame_size - 1) / frame_size;
        memory_size = pages * frame_size;

        // Create process without allocating memory immediately
        // Memory will be allocated when the process is scheduled to run
        Process* p = new Process(name, instructions, memory_size);
        addProcess(p);

        uint64_t target_cycle = cpu_cycles + batch_frequency;
        while (cpu_cycles < target_cycle && !stop_batch) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

void Scheduler::schedule() {
    while (!stop_requested) {
        std::unique_lock<std::mutex> queue_lock(queue_mutex);

        // Check if there are processes waiting in the queue
        if (process_queue.empty()) {
            queue_lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Find an available core
        std::unique_lock<std::mutex> core_lock(cores_mutex);
        int available_core = -1;
        for (int i = 0; i < num_cores; i++) {
            if (cores[i] == nullptr) {
                available_core = i;
                break;
            }
        }

        if (available_core == -1) {
            // No cores available, wait
            core_lock.unlock();
            queue_lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Try to assign processes to available cores
        bool assigned = false;
        std::queue<Process*> temp_queue;

        while (!process_queue.empty() && !assigned) {
            Process* p = process_queue.front();
            process_queue.pop();

            // Check if process already has memory allocated
            if (isProcessMemoryAllocated(p)) {
                // Process already has memory allocated, assign to core
                cores[available_core] = p;
                p->state = ProcessState::Running;
                p->core_id = available_core;
                quantum_counters[available_core] = 0;
                if (p->start_time.time_since_epoch().count() == 0) {
                    p->start_time = std::chrono::system_clock::now();
                }
                assigned = true;
            }
            else {
                // Try to allocate memory for the process
                uint64_t required_memory = p->getMemorySize();
                uint64_t available_memory = memory_manager.getFreeMemory();

                if (memory_manager.allocateProcess(p, required_memory)) {
                    // Memory allocation successful, can assign to core
                    cores[available_core] = p;
                    p->state = ProcessState::Running;
                    p->core_id = available_core;
                    quantum_counters[available_core] = 0;
                    if (p->start_time.time_since_epoch().count() == 0) {
                        p->start_time = std::chrono::system_clock::now();
                    }
                    assigned = true;
                }
                else {
                    // Memory allocation failed, put back in temp queue to try later
                    temp_queue.push(p);
                }
            }
        }

        // Put back any processes that couldn't be scheduled due to memory constraints
        while (!temp_queue.empty()) {
            process_queue.push(temp_queue.front());
            temp_queue.pop();
        }

        core_lock.unlock();
        queue_lock.unlock();

        if (!assigned) {
            // No process could be scheduled, wait a bit longer
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

bool Scheduler::isProcessMemoryAllocated(Process* process) {
    return memory_manager.isProcessAllocated(process);
}

void Scheduler::worker(int core_id) {
    while (!stop_requested) {
        Process* p = nullptr;
        {
            std::lock_guard<std::mutex> lock(cores_mutex);
            p = cores[core_id];
        }

        if (p) {
            // Double-check that process still has memory allocated
            if (!isProcessMemoryAllocated(p)) {
                // Process lost memory allocation, remove from core and put back in queue
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_queue.push(p);
                    p->state = ProcessState::Waiting;
                }
                {
                    std::lock_guard<std::mutex> lock(cores_mutex);
                    cores[core_id] = nullptr;
                }
                quantum_counters[core_id] = 0;
                continue;
            }

            if (p->isSleeping()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_per_exec));
                continue;
            }

            p->state = ProcessState::Running;

            bool finished = p->executeNextInstruction(core_id, &memory_manager);

            if (delay_per_exec > 0) {
                uint64_t target_cycle = cpu_cycles + delay_per_exec;
                while (cpu_cycles < target_cycle && !stop_requested) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }

            if (finished || p->state == ProcessState::Finished) {
                // Deallocate memory before marking as finished
                memory_manager.deallocateProcess(p);
                p->state = ProcessState::Finished;
                {
                    std::lock_guard<std::mutex> lock(finished_mutex);
                    finished_processes.push_back(p);
                }
                {
                    std::lock_guard<std::mutex> lock(cores_mutex);
                    cores[core_id] = nullptr;
                }
                quantum_counters[core_id] = 0;
                continue;
            }

            // Handle round-robin scheduling if configured
            if (scheduler_type == "rr") {
                quantum_counters[core_id]++;
                current_quantum++;

                if (quantum_counters[core_id] >= quantum_cycles) {
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_queue.push(p);
                        p->state = ProcessState::Waiting;
                    }
                    {
                        std::lock_guard<std::mutex> lock(cores_mutex);
                        cores[core_id] = nullptr;
                    }
                    quantum_counters[core_id] = 0;
                }
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}