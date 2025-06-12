#include "scheduler.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <random>

Scheduler::Scheduler(int num_cores) 
    : num_cores(num_cores), cores(num_cores, nullptr), 
      stop_requested(false), is_running(false) {}

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
        if (cores[i] != nullptr) {
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
    } else {
        out = &std::cout;
    }
    
    *out << "--------------------------------------" << std::endl;
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
             << formatTimePoint(p->start_time)
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
    
    while (!stop_batch) {
        // Generate a new process
        std::string name = "p" + std::to_string(process_counter++);
        uint64_t instructions = dist(gen);
        Process* p = new Process(name, instructions);
        addProcess(p);
        
        // Sleep for batch frequency (simulated)
        for (uint64_t i = 0; i < batch_frequency && !stop_batch; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void Scheduler::schedule() {
    while (!stop_requested) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!process_queue.empty()) {
            Process* p = process_queue.front();
            process_queue.pop();
            lock.unlock();

            bool assigned = false;
            while (!assigned && !stop_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> core_lock(cores_mutex);
                for (int i = 0; i < num_cores; i++) {
                    if (cores[i] == nullptr) {
                        cores[i] = p;
                        p->state = ProcessState::Running;
                        p->core_id = i;
                        assigned = true;
                        break;
                    }
                }
            }
        }
        else {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Scheduler::worker(int core_id) {
    while (!stop_requested) {
        Process* p = nullptr;
        {
            std::lock_guard<std::mutex> lock(cores_mutex);
            p = cores[core_id];
        }
        if (p) {
            // Simulate delay
            for (uint64_t i = 0; i < delay_per_exec; i++) {
                if (stop_requested) break;
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
            
            p->remaining_instructions--;

            if (p->remaining_instructions <= 0) {
                p->state = ProcessState::Finished;
                p->end_time = std::chrono::system_clock::now();
                {
                    std::lock_guard<std::mutex> lock(finished_mutex);
                    finished_processes.push_back(p);
                }
                {
                    std::lock_guard<std::mutex> lock(cores_mutex);
                    cores[core_id] = nullptr;
                }
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}