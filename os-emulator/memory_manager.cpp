#include "memory_manager.h"
#include "process.h"
#include <algorithm>
#include <cstdio>

DemandPagingMemoryManager::DemandPagingMemoryManager(uint64_t total_mem, uint64_t frame_size,
    uint64_t min_proc_mem, uint64_t max_proc_mem)
    : total_memory(total_mem), frame_size(frame_size),
    min_proc_memory(min_proc_mem), max_proc_memory(max_proc_mem),
    num_frames(total_mem / frame_size) {

    frames.resize(num_frames);
    for (uint64_t i = 0; i < num_frames; i++) {
        frames[i].frame_number = i;
    }

    // Initialize simulated physical memory
    physical_memory.resize(total_memory, 0);

    initializeBackingStore();
}

DemandPagingMemoryManager::~DemandPagingMemoryManager() {
    // Cleanup pages
    for (auto& frame : frames) {
        if (frame.page) {
            delete frame.page;
        }
    }
}

void DemandPagingMemoryManager::initializeBackingStore() {
    std::ofstream backing_store(backing_store_file, std::ios::binary | std::ios::trunc);
    if (backing_store.is_open()) {
        backing_store.close();
    }
}

bool DemandPagingMemoryManager::allocateProcess(Process* process, uint64_t required_memory) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Check if process is already allocated
    if (process_memory_sizes.find(process) != process_memory_sizes.end()) {
        return true; // Already allocated
    }

    // Calculate total virtual memory already allocated
    uint64_t total_allocated = 0;
    for (const auto& pair : process_memory_sizes) {
        total_allocated += pair.second;
    }

    // Check if adding this process would exceed physical memory
    if (total_allocated + required_memory > total_memory) {
        return false;
    }

    // Check if memory is power of 2 and within range [2^6, 2^16]
    if (required_memory & (required_memory - 1)) return false; // Not power of 2
    if (required_memory < 64 || required_memory > 65536) return false; // Not in range

    process_memory_sizes[process] = required_memory;
    page_tables[process] = std::unordered_map<uint64_t, PageTableEntry>();

    return true;
}

void DemandPagingMemoryManager::deallocateProcess(Process* process) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Free all frames used by this process
    for (uint64_t i = 0; i < frames.size(); i++) {
        if (frames[i].page && frames[i].page->owner == process) {
            pageOut(i);  // Call pageOut to properly increment count
        }
    }

    // Remove from page tables and memory sizes
    page_tables.erase(process);
    process_memory_sizes.erase(process);
}

bool DemandPagingMemoryManager::isProcessAllocated(Process* process) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    return process_memory_sizes.find(process) != process_memory_sizes.end();
}

bool DemandPagingMemoryManager::readMemory(Process* process, uint64_t virtual_address, uint16_t& value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (!isValidAddress(process, virtual_address)) {
        return false; // Invalid memory address
    }

    uint64_t page_number = getPageNumber(virtual_address);
    uint64_t offset = getOffset(virtual_address);

    auto& page_table = page_tables[process];
    auto it = page_table.find(page_number);

    if (it == page_table.end() || !it->second.valid) {
        // Page fault - need to handle
        if (!handlePageFault(process, virtual_address)) {
            return false; // Could not handle page fault
        }
        it = page_table.find(page_number);
    }

    // Update access time
    it->second.last_access = std::chrono::system_clock::now();
    frames[it->second.frame_number].last_access = it->second.last_access;

    // Read from physical memory
    uint64_t physical_addr = getPhysicalAddress(it->second.frame_number, offset);
    if (physical_addr + sizeof(uint16_t) <= physical_memory.size()) {
        value = *reinterpret_cast<uint16_t*>(&physical_memory[physical_addr]);
    }
    else {
        value = 0; // Uninitialized memory
    }

    return true;
}

bool DemandPagingMemoryManager::writeMemory(Process* process, uint64_t virtual_address, uint16_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (!isValidAddress(process, virtual_address)) {
        return false; // Invalid memory address
    }

    uint64_t page_number = getPageNumber(virtual_address);
    uint64_t offset = getOffset(virtual_address);

    auto& page_table = page_tables[process];
    auto it = page_table.find(page_number);

    if (it == page_table.end() || !it->second.valid) {
        // Page fault - need to handle
        if (!handlePageFault(process, virtual_address)) {
            return false; // Could not handle page fault
        }
        it = page_table.find(page_number);
    }

    // Update access time and mark as dirty
    it->second.last_access = std::chrono::system_clock::now();
    it->second.dirty = true;
    frames[it->second.frame_number].last_access = it->second.last_access;
    if (frames[it->second.frame_number].page) {
        frames[it->second.frame_number].page->dirty = true;
    }

    // Write to physical memory
    uint64_t physical_addr = getPhysicalAddress(it->second.frame_number, offset);
    if (physical_addr + sizeof(uint16_t) <= physical_memory.size()) {
        *reinterpret_cast<uint16_t*>(&physical_memory[physical_addr]) = value;
    }

    return true;
}

bool DemandPagingMemoryManager::handlePageFault(Process* process, uint64_t virtual_address) {
    uint64_t page_number = getPageNumber(virtual_address);

    // Find a free frame or select a victim
    int frame_idx = findFreeFrame();
    if (frame_idx == -1) {
        frame_idx = selectVictimFrame();
        if (frame_idx == -1) {
            return false; // No frames available
        }

        // Page out the victim
        pageOut(frame_idx);
    }

    // Page in the required page
    return pageIn(process, page_number, frame_idx);
}

int DemandPagingMemoryManager::findFreeFrame() {
    for (size_t i = 0; i < frames.size(); i++) {
        if (!frames[i].allocated) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int DemandPagingMemoryManager::selectVictimFrame() {
    // LRU page replacement
    int victim = -1;
    auto oldest_time = std::chrono::system_clock::now();

    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i].allocated && frames[i].last_access < oldest_time) {
            oldest_time = frames[i].last_access;
            victim = static_cast<int>(i);
        }
    }

    return victim;
}

void DemandPagingMemoryManager::pageOut(uint64_t frame_number) {
    Frame& frame = frames[frame_number];
    if (!frame.page) return;

    Page* page = frame.page;
    Process* process = page->owner;

    // If page is dirty, write to backing store
    if (page->dirty) {
        std::vector<uint8_t> data(frame_size);
        uint64_t physical_addr = getPhysicalAddress(frame_number, 0);
        std::copy(physical_memory.begin() + physical_addr,
            physical_memory.begin() + physical_addr + frame_size,
            data.begin());
        writeToBackingStore(process, page->page_number, data);
    }

    // Always increment pages_out count regardless of dirty state
    pages_out++;  // Increment count for all page outs

    // Update page table
    if (page_tables.find(process) != page_tables.end()) {
        auto& page_table = page_tables[process];
        page_table[page->page_number].valid = false;
    }

    // Free the frame
    frame.allocated = false;
    delete frame.page;
    frame.page = nullptr;
}

bool DemandPagingMemoryManager::pageIn(Process* process, uint64_t page_number, uint64_t frame_number) {
    Frame& frame = frames[frame_number];

    // Create new page
    Page* page = new Page();
    page->page_number = page_number;
    page->virtual_address = page_number * frame_size;
    page->owner = process;
    page->valid = true;
    page->last_access = std::chrono::system_clock::now();

    // Try to read from backing store
    std::vector<uint8_t> data(frame_size, 0);
    readFromBackingStore(process, page_number, data);

    // Load into physical memory
    uint64_t physical_addr = getPhysicalAddress(frame_number, 0);
    std::copy(data.begin(), data.end(), physical_memory.begin() + physical_addr);

    // Update frame
    frame.page = page;
    frame.allocated = true;
    frame.last_access = page->last_access;

    // Update page table
    auto& page_table = page_tables[process];
    page_table[page_number].frame_number = frame_number;
    page_table[page_number].valid = true;
    page_table[page_number].dirty = false;
    page_table[page_number].last_access = page->last_access;

    pages_in++;
    return true;
}

bool DemandPagingMemoryManager::isValidAddress(Process* process, uint64_t virtual_address) {
    auto it = process_memory_sizes.find(process);
    if (it == process_memory_sizes.end()) {
        return false;
    }
    return virtual_address < it->second;
}

uint64_t DemandPagingMemoryManager::getUsedMemory() const {
    uint64_t used = 0;
    for (const auto& frame : frames) {
        if (frame.allocated) {
            used += frame_size;
        }
    }
    return used;
}

bool DemandPagingMemoryManager::readFromBackingStore(Process* process, uint64_t page_number, std::vector<uint8_t>& data) {
    std::ifstream backing_store(backing_store_file, std::ios::binary);
    if (!backing_store.is_open()) {
        // If backing store doesn't exist or can't open, return zeros
        std::fill(data.begin(), data.end(), 0);
        return true;
    }

    // Create a unique identifier for this page
    std::string page_id = process->name + "_" + std::to_string(page_number);

    // Simple format: page_id followed by data
    std::string line;
    bool found = false;

    while (std::getline(backing_store, line)) {
        if (line.find(page_id) == 0) {
            // Found the page, read the data
            size_t data_start = page_id.length() + 1; // +1 for delimiter
            if (data_start < line.length()) {
                // Convert hex string back to bytes
                for (size_t i = 0; i < std::min(data.size(), (line.length() - data_start) / 2); i++) {
                    std::string hex_byte = line.substr(data_start + i * 2, 2);
                    data[i] = static_cast<uint8_t>(std::stoul(hex_byte, nullptr, 16));
                }
            }
            found = true;
            break;
        }
    }

    if (!found) {
        // Page not in backing store, initialize with zeros
        std::fill(data.begin(), data.end(), 0);
    }

    backing_store.close();
    return true;
}

void DemandPagingMemoryManager::writeToBackingStore(Process* process, uint64_t page_number, const std::vector<uint8_t>& data) {
    std::string page_id = process->name + "_" + std::to_string(page_number);

    // Read existing content
    std::vector<std::string> lines;
    std::ifstream backing_store_read(backing_store_file);
    std::string line;
    bool found = false;

    while (std::getline(backing_store_read, line)) {
        if (line.find(page_id) == 0) {
            // Replace this line
            std::string new_line = page_id + ":";
            for (uint8_t byte : data) {
                char hex[3];
                // Replace sprintf with snprintf
                snprintf(hex, sizeof(hex), "%02X", byte);
                new_line += hex;
            }
            lines.push_back(new_line);
            found = true;
        }
        else {
            lines.push_back(line);
        }
    }
    backing_store_read.close();

    if (!found) {
        // Add new entry
        std::string new_line = page_id + ":";
        for (uint8_t byte : data) {
            char hex[3];
            // Replace sprintf with snprintf
            snprintf(hex, sizeof(hex), "%02X", byte);
            new_line += hex;
        }
        lines.push_back(new_line);
    }

    // Write back to file
    std::ofstream backing_store_write(backing_store_file);
    for (const std::string& l : lines) {
        backing_store_write << l << std::endl;
    }
    backing_store_write.close();
}

void DemandPagingMemoryManager::generateProcessSMI() {
    std::lock_guard<std::mutex> lock(memory_mutex);

    std::cout << "===============================================" << std::endl;
    std::cout << "| PROCESS-SMI V1.0 CSOPESY                   |" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "CPU Utilization: Not available" << std::endl;
    std::cout << "Memory Usage: " << getUsedMemory() << " / " << getTotalMemory() << " bytes" << std::endl;
    std::cout << "Memory Utilization: " << std::fixed << std::setprecision(1)
        << (static_cast<double>(getUsedMemory()) / getTotalMemory() * 100.0) << "%" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "Running processes and memory usage:" << std::endl;
    std::cout << "-----------------------------------------------" << std::endl;

    for (const auto& [process, memory_size] : process_memory_sizes) {
        uint64_t pages_used = 0;
        for (const auto& frame : frames) {
            if (frame.allocated && frame.page && frame.page->owner == process) {
                pages_used++;
            }
        }

        std::cout << "Process: " << process->name << std::endl;
        std::cout << "Memory Usage: " << (pages_used * frame_size) << " / " << memory_size << " bytes" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;
    }
}

void DemandPagingMemoryManager::generateVMStat() {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Dummy values for demonstration
    uint64_t idle_ticks = 0;
    uint64_t active_ticks = 0;

    std::cout << "===============================================" << std::endl;
    std::cout << "| VMSTAT                                      |" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "Total memory: " << getTotalMemory() << " bytes" << std::endl;
    std::cout << "Used memory: " << getUsedMemory() << " bytes" << std::endl;
    std::cout << "Free memory: " << getFreeMemory() << " bytes" << std::endl;
    std::cout << "Idle cpu ticks: " << idle_ticks << std::endl;
    std::cout << "Active cpu ticks: " << active_ticks << std::endl;
    std::cout << "Total cpu ticks: " << (idle_ticks + active_ticks) << std::endl;
    std::cout << "Num paged in: " << getNumPagesIn() << std::endl;
    std::cout << "Num paged out: " << getNumPagesOut() << std::endl;
    std::cout << "===============================================" << std::endl;
}