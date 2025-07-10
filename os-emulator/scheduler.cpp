#include "scheduler.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <random>

Scheduler::Scheduler(int num_cores, uint64_t total_mem, uint64_t frame_size, uint64_t proc_mem)
    : num_cores(num_cores), cores(num_cores, nullptr),
    stop_requested(false), is_running(false),
    memory_manager(total_mem, frame_size, proc_mem) {}

Scheduler::~Scheduler() {
    stop();
    stopBatchProcess();
    // Cleanup processes
    for (auto& p : all_processes) {
        delete p.second;
    }
}

bool MemoryManager::allocateFirstFit(Process* p) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    for (auto it = memory_blocks.begin(); it != memory_blocks.end(); ++it) {
        if (!it->allocated && (it->end - it->start + 1) >= proc_memory) {
            uint64_t remaining = (it->end - it->start + 1) - proc_memory;
            if (remaining > 0) {
                MemoryBlock new_block = { it->start + proc_memory, it->end, nullptr, false };
                memory_blocks.insert(std::next(it), new_block);
            }
            it->end = it->start + proc_memory - 1;
            it->process = p;
            it->allocated = true;
            p->memory_start = it->start;
            p->memory_end = it->end;
            return true;
        }
    }
    return false;
}

void MemoryManager::deallocate(Process* p) {
    std::lock_guard<std::mutex> lock(mem_mutex);
    for (auto it = memory_blocks.begin(); it != memory_blocks.end(); ++it) {
        if (it->process == p) {
            it->allocated = false;
            it->process = nullptr;

            if (it != memory_blocks.begin()) {
                auto prev = std::prev(it);
                if (!prev->allocated) {
                    prev->end = it->end;
                    memory_blocks.erase(it);
                    it = prev;
                }
            }

            auto next = std::next(it);
            if (next != memory_blocks.end() && !next->allocated) {
                it->end = next->end;
                memory_blocks.erase(next);
            }
            break;
        }
    }
}

void MemoryManager::generateMemorySnapshot(uint64_t quantum, const std::string& timestamp) {
    std::string folder = "memory_snapshots/";
    std::string filename = folder + "memory_stamp_" + std::to_string(quantum) + ".txt";

    // Create the folder if it doesn't exist (optional)
    std::filesystem::create_directories(folder);

    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    file << "Timestamp: " << timestamp << "\n";

    int process_count = 0;
    uint64_t external_frag = 0;

    for (const auto& block : memory_blocks) {
        if (!block.allocated) {
            external_frag += (block.end - block.start + 1);
        }
        else {
            process_count++;
        }
    }

    file << "Number of processes in memory: " << process_count << "\n";
    file << "Total external fragmentation in KB: " << (external_frag / 1024) << "\n\n";

    file << "----end---- = " << total_memory << " (max-overall-mem)\n\n";

    for (auto it = memory_blocks.rbegin(); it != memory_blocks.rend(); ++it) {
        file << it->end + 1 << "\n";
        if (it->allocated) {
            file << it->process->name << "\n";
        }
        file << it->start << "\n\n";
    }

    file << "----start---- = 0\n";
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
        //if (cores[i] != nullptr) {
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
                    << done << " / " << p->total_instructions << std::endl;
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

 // VER 1
void Scheduler::batchWorker() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(min_instructions, max_instructions);

    while (!stop_batch) {
        // Generate a new process
        std::string name = "p" + std::to_string(process_counter++);
        uint64_t instructions = dist(gen);
        Process* p = new Process(name, instructions);
        addProcess(p);

        // Sleep for batch frequency (simulated)
        /*for (uint64_t i = 0; i < batch_frequency && !stop_batch; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }*/
        uint64_t target_cycle = cpu_cycles + batch_frequency;
        while (cpu_cycles < target_cycle && !stop_batch) {
            std::this_thread::sleep_for(std::chrono::microseconds(10)); // Yield a little
        }
    }
} 
/*
// VER 2
void Scheduler::batchWorker() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(min_instructions, max_instructions);
    uint64_t cycles_waited = 0;

    while (!stop_batch) {
        // Wait for the required number of CPU cycles
        while (cycles_waited < batch_frequency && !stop_batch) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cycles_waited++;
        }

        if (stop_batch) break;

        // Generate a new process
        std::string name = "p" + std::to_string(process_counter++);
        uint64_t instructions = dist(gen);
        Process* p = new Process(name, instructions);
        addProcess(p);

        cycles_waited = 0;
    }
}*/

// most robust VER so far
//void Scheduler::batchWorker() {
//    std::random_device rd;
//    std::mt19937 gen(rd());
//    std::uniform_int_distribution<uint64_t> dist(min_instructions, max_instructions);
//
//    uint64_t last_cycle = cpu_cycles.load();
//
//    while (!stop_batch) {
//        // Wait for the required number of CPU cycles
//        while ((cpu_cycles.load() - last_cycle) < batch_frequency && !stop_batch) {
//            std::this_thread::sleep_for(std::chrono::milliseconds(10));
//        }
//
//        if (stop_batch) break;
//
//        // Generate a new process
//        std::string name = "p" + std::to_string(process_counter++);
//        uint64_t instructions = dist(gen);
//        Process* p = new Process(name, instructions);
//        addProcess(p);
//
//        last_cycle = cpu_cycles.load();
//    }
//}

void Scheduler::schedule() {
    while (!stop_requested) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!process_queue.empty()) {
            Process* p = process_queue.front();
            process_queue.pop();
            lock.unlock();

            bool assigned = false;
            while (!assigned && !stop_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::lock_guard<std::mutex> core_lock(cores_mutex);
                for (int i = 0; i < num_cores; i++) {
                    if (cores[i] == nullptr) {
                        cores[i] = p;
                        p->state = ProcessState::Running;
                        p->core_id = i;
                        quantum_counters[i] = 0;
                        if (p->start_time.time_since_epoch().count() == 0) {
                            p->start_time = std::chrono::system_clock::now();
                        }
                        assigned = true;
                        break;
                    }
                }
            }
        }
        else {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

/*
void Scheduler::worker(int core_id) {
    while (!stop_requested) {
        Process* p = nullptr;

        // Check if core has a process assigned
        {
            std::lock_guard<std::mutex> lock(cores_mutex);
            p = cores[core_id];
        }

        if (p) {
            // If process is sleeping, wait
            if (p->isSleeping()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            p->state = ProcessState::Running;

            // Execute one instruction
            if (p->executeNextInstruction(core_id)) {
                // Process finished
                p->state = ProcessState::Finished;
                {
                    std::lock_guard<std::mutex> lock(finished_mutex);
                    finished_processes.push_back(p);
                }
                {
                    std::lock_guard<std::mutex> lock(cores_mutex);
                    cores[core_id] = nullptr;
                }
                quantum_counters[core_id] = 0; // Reset counter
                continue;
            }

            // Round Robin preemption check
            if (scheduler_type == "rr") {
                quantum_counters[core_id]++;

                if (quantum_counters[core_id] >= quantum_cycles) {
                    // Preempt process
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_queue.push(p);
                        p->state = ProcessState::Waiting;
                    }
                    {
                        std::lock_guard<std::mutex> lock(cores_mutex);
                        cores[core_id] = nullptr;
                    }
                    quantum_counters[core_id] = 0; // Reset counter
                }
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}*/

void Scheduler::worker(int core_id) {
    while (!stop_requested) {
        Process* p = nullptr;
        {
            std::lock_guard<std::mutex> lock(cores_mutex);
            p = cores[core_id];
        }

        if (p) {
            if (p->isSleeping()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_per_exec));
                continue;
            }

            if (p->memory_start == 0 && p->memory_end == 0) {
                if (!memory_manager.allocateFirstFit(p)) {
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_queue.push(p);
                        p->state = ProcessState::Waiting;
                    }
                    {
                        std::lock_guard<std::mutex> lock(cores_mutex);
                        cores[core_id] = nullptr;
                    }
                    continue;
                }
            }

            p->state = ProcessState::Running;

            if (current_quantum % quantum_cycles == 0) {
                memory_manager.generateMemorySnapshot(
                    current_quantum,
                    formatTimePoint(std::chrono::system_clock::now())
                );
            }

            bool finished = p->executeNextInstruction(core_id);

            if (delay_per_exec > 0) {
                uint64_t target_cycle = cpu_cycles + delay_per_exec;
                while (cpu_cycles < target_cycle && !stop_requested) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }

            if (finished) {
                memory_manager.deallocate(p);
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