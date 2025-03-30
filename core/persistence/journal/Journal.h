#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "JournalEntry.h"

namespace pinnacle {
namespace persistence {
namespace journal {

class Journal {
public:
    // Constructor with journal file path
    explicit Journal(const std::string& journalPath);
    ~Journal();
    
    // Append a new entry to the journal
    bool appendEntry(const JournalEntry& entry);
    
    // Read all entries from the journal
    std::vector<JournalEntry> readAllEntries();
    
    // Read entries after a specific sequence number
    std::vector<JournalEntry> readEntriesAfter(uint64_t sequenceNumber);
    
    // Get the latest sequence number
    uint64_t getLatestSequenceNumber() const;
    
    // Compact the journal (remove entries before a checkpoint)
    bool compact(uint64_t checkpointSequence);
    
    // Flush pending entries to disk
    void flush();
    
private:
    // Journal file path
    std::string m_journalPath;
    
    // Memory-mapped file
    void* m_mappedMemory = nullptr;
    size_t m_mappedSize = 0;
    int m_fileDescriptor = -1;
    
    // Current write position
    std::atomic<size_t> m_writePosition{0};
    
    // Latest sequence number
    std::atomic<uint64_t> m_latestSequence{0};
    
    // Mutex for write operations
    std::mutex m_writeMutex;
    
    // Map the journal file into memory
    bool mapFile();
    
    // Unmap the journal file
    void unmapFile();
    
    // Resize the mapped file if needed
    bool resizeIfNeeded(size_t additionalSize);
    
    // Initial journal file size in bytes (10MB)
    static constexpr size_t INITIAL_FILE_SIZE = 10 * 1024 * 1024;
    
    // Maximum journal file size in bytes (1GB)
    static constexpr size_t MAX_FILE_SIZE = 1024 * 1024 * 1024;
    
    // Size increment when resizing (10MB)
    static constexpr size_t SIZE_INCREMENT = 10 * 1024 * 1024;
};

} // namespace journal
} // namespace persistence
} // namespace pinnacle