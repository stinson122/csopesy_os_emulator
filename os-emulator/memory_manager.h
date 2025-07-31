#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <queue>
#include <chrono>
#include <string>
#include <atomic>
#include <iomanip>
#include <iostream>

class Process; // Forward declaration

struct Page {
    uint64_t page_number;
    uint64_t virtual_address;
    bool dirty = false;
    bool valid = false;
    std::chrono::system_clock::time_point last_access;
    Process* owner = nullptr;
};

struct Frame {
    uint64_t frame_number;
    Page* page = nullptr;
    bool allocated = false;
    std::chrono::system_clock::time_point last_access;
};

struct PageTableEntry {
    uint64_t frame_number = 0;
    bool valid = false;
    bool dirty = false;
    std::chrono::system_clock::time_point last_access;
};

class DemandPagingMemoryManager {
public:
    DemandPagingMemoryManager(uint64_t total_mem, uint64_t frame_size, uint64_t min_proc_mem, uint64_t max_proc_mem);
    ~DemandPagingMemoryManager();

    // Memory allocation and deallocation
    bool allocateProcess(Process* process, uint64_t required_memory);
    void deallocateProcess(Process* process);
    bool isProcessAllocated(Process* process) const;  // New method to check allocation

    // Memory access operations
    bool readMemory(Process* process, uint64_t virtual_address, uint16_t& value);
    bool writeMemory(Process* process, uint64_t virtual_address, uint16_t value);

    // Page fault handling
    bool handlePageFault(Process* process, uint64_t virtual_address);

    // Backing store operations
    void pageOut(uint64_t frame_number);
    bool pageIn(Process* process, uint64_t page_number, uint64_t frame_number);

    // Memory statistics
    uint64_t getTotalMemory() const { return total_memory; }
    uint64_t getUsedMemory() const;
    uint64_t getFreeMemory() const { return total_memory - getUsedMemory(); }
    uint64_t getNumPagesIn() const { return pages_in.load(); }
    uint64_t getNumPagesOut() const { return pages_out.load(); }

    // Add frame size getter
    uint64_t getFrameSize() const { return frame_size; }

    // Memory visualization
    void generateProcessSMI();
    void generateVMStat();

    // Frame management
    int findFreeFrame();
    int selectVictimFrame(); // LRU page replacement

    // Validation
    bool isValidAddress(Process* process, uint64_t virtual_address);

private:
    uint64_t total_memory;
    uint64_t frame_size;
    uint64_t min_proc_memory;
    uint64_t max_proc_memory;
    uint64_t num_frames;

    std::vector<Frame> frames;
    std::unordered_map<Process*, std::unordered_map<uint64_t, PageTableEntry>> page_tables;
    std::unordered_map<Process*, uint64_t> process_memory_sizes;

    std::atomic<uint64_t> pages_in{ 0 };
    std::atomic<uint64_t> pages_out{ 0 };

    mutable std::mutex memory_mutex;
    std::string backing_store_file = "csopesy-backing-store.txt";

    // Helper functions
    uint64_t getPageNumber(uint64_t virtual_address) { return virtual_address / frame_size; }
    uint64_t getOffset(uint64_t virtual_address) { return virtual_address % frame_size; }
    uint64_t getPhysicalAddress(uint64_t frame_number, uint64_t offset) { return frame_number * frame_size + offset; }

    // Backing store management
    void initializeBackingStore();
    bool readFromBackingStore(Process* process, uint64_t page_number, std::vector<uint8_t>& data);
    void writeToBackingStore(Process* process, uint64_t page_number, const std::vector<uint8_t>& data);

    // Memory data storage (simulated physical memory)
    std::vector<uint8_t> physical_memory;
};

#endif // MEMORY_MANAGER_H#pragma once