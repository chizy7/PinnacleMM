#include "Journal.h"
#include "../../utils/TimeUtils.h"

#include <iostream>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

namespace pinnacle {
namespace persistence {
namespace journal {

Journal::Journal(const std::string& journalPath)
    : m_journalPath(journalPath) {
    // Create directory if it doesn't exist
    std::filesystem::path path(journalPath);
    std::filesystem::create_directories(path.parent_path());
    
    // Initialize memory-mapped file
    if (!mapFile()) {
        throw std::runtime_error("Failed to initialize journal file: " + journalPath);
    }
}

Journal::~Journal() {
    // Ensure journal is flushed before closing
    flush();
    
    // Unmap memory
    unmapFile();
}

bool Journal::appendEntry(const JournalEntry& entry) {
    // Get serialized entry
    auto serializedEntry = entry.serialize();
    
    // Lock for thread safety
    std::lock_guard<std::mutex> lock(m_writeMutex);
    
    // Check if we need to resize
    if (!resizeIfNeeded(serializedEntry.size())) {
        return false;
    }
    
    // Set sequence number
    JournalEntry mutableEntry = entry;
    mutableEntry.setSequenceNumber(m_latestSequence.load(std::memory_order_relaxed) + 1);
    
    // Re-serialize with correct sequence number
    serializedEntry = mutableEntry.serialize();
    
    // Get write position
    size_t position = m_writePosition.load(std::memory_order_relaxed);
    
    // Write to memory-mapped file
    std::memcpy(static_cast<uint8_t*>(m_mappedMemory) + position, 
              serializedEntry.data(), serializedEntry.size());
    
    // Update write position
    m_writePosition.store(position + serializedEntry.size(), std::memory_order_release);
    
    // Update sequence number
    m_latestSequence.store(mutableEntry.getHeader().sequenceNumber, std::memory_order_release);
    
    return true;
}

std::vector<JournalEntry> Journal::readAllEntries() {
    return readEntriesAfter(0);
}

std::vector<JournalEntry> Journal::readEntriesAfter(uint64_t sequenceNumber) {
    std::vector<JournalEntry> entries;
    
    // Read-only, no need for lock
    size_t position = 0;
    size_t endPosition = m_writePosition.load(std::memory_order_acquire);
    
    while (position < endPosition) {
        // Ensure there's enough space for at least the header
        if (position + sizeof(JournalEntryHeader) > endPosition) {
            break;
        }
        
        // Read header
        JournalEntryHeader header;
        std::memcpy(&header, static_cast<uint8_t*>(m_mappedMemory) + position, 
                  sizeof(JournalEntryHeader));
        
        // Skip entries with sequence number less than or equal to requested
        if (header.sequenceNumber <= sequenceNumber) {
            position += sizeof(JournalEntryHeader) + header.entrySize;
            continue;
        }
        
        // Ensure entry is valid
        if (position + sizeof(JournalEntryHeader) + header.entrySize > endPosition) {
            break;
        }
        
        // Read the entire entry
        try {
            JournalEntry entry = JournalEntry::deserialize(
                static_cast<uint8_t*>(m_mappedMemory) + position,
                sizeof(JournalEntryHeader) + header.entrySize);
            
            // Add to result
            entries.push_back(entry);
        } catch (const std::exception& e) {
            // Log error and continue
            std::cerr << "Error deserializing journal entry: " << e.what() << std::endl;
        }
        
        // Advance position
        position += sizeof(JournalEntryHeader) + header.entrySize;
    }
    
    return entries;
}

uint64_t Journal::getLatestSequenceNumber() const {
    return m_latestSequence.load(std::memory_order_acquire);
}

bool Journal::compact(uint64_t checkpointSequence) {
    // Lock for thread safety
    std::lock_guard<std::mutex> lock(m_writeMutex);
    
    // Create a temporary file path
    std::string tempPath = m_journalPath + ".tmp";
    
    // Open temporary file
    int tempFd = open(tempPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (tempFd == -1) {
        return false;
    }
    
    // Resize the temporary file
    if (ftruncate(tempFd, INITIAL_FILE_SIZE) == -1) {
        close(tempFd);
        return false;
    }
    
    // Map the temporary file
    void* tempMemory = mmap(nullptr, INITIAL_FILE_SIZE, PROT_READ | PROT_WRITE, 
                          MAP_SHARED, tempFd, 0);
    if (tempMemory == MAP_FAILED) {
        close(tempFd);
        return false;
    }
    
    // Current position in the temp file
    size_t tempPosition = 0;
    
    // Copy entries after checkpoint sequence
    std::vector<JournalEntry> entries = readEntriesAfter(checkpointSequence);
    for (const auto& entry : entries) {
        auto serializedEntry = entry.serialize();
        
        // Check if we need to resize
        if (tempPosition + serializedEntry.size() > INITIAL_FILE_SIZE) {
            // Need more space
            size_t newSize = ((tempPosition + serializedEntry.size()) / SIZE_INCREMENT + 1) * SIZE_INCREMENT;
            
            // Resize the temporary file
            if (ftruncate(tempFd, newSize) == -1) {
                munmap(tempMemory, INITIAL_FILE_SIZE);
                close(tempFd);
                return false;
            }
            
            // Remap the file
            munmap(tempMemory, INITIAL_FILE_SIZE);
            tempMemory = mmap(nullptr, newSize, PROT_READ | PROT_WRITE, MAP_SHARED, tempFd, 0);
            if (tempMemory == MAP_FAILED) {
                close(tempFd);
                return false;
            }
        }
        
        // Copy entry to temp file
        std::memcpy(static_cast<uint8_t*>(tempMemory) + tempPosition, 
                  serializedEntry.data(), serializedEntry.size());
        
        tempPosition += serializedEntry.size();
    }
    
    // Sync the temporary file
    msync(tempMemory, tempPosition, MS_SYNC);
    
    // Unmap and close the temporary file
    munmap(tempMemory, INITIAL_FILE_SIZE);
    close(tempFd);
    
    // Unmap the current file
    unmapFile();
    
    // Rename the temporary file to the current file
    if (rename(tempPath.c_str(), m_journalPath.c_str()) == -1) {
        // Failed to rename, try to restore original file
        mapFile();
        return false;
    }
    
    // Remap the new file
    if (!mapFile()) {
        return false;
    }
    
    // Update write position
    m_writePosition.store(tempPosition, std::memory_order_release);
    
    return true;
}

void Journal::flush() {
    // Sync memory-mapped file to disk
    if (m_mappedMemory != nullptr) {
        size_t writePosition = m_writePosition.load(std::memory_order_acquire);
        msync(m_mappedMemory, writePosition, MS_SYNC);
    }
}

bool Journal::mapFile() {
    // Check if file exists
    struct stat statBuf;
    bool fileExists = (stat(m_journalPath.c_str(), &statBuf) == 0);
    
    // Open or create the file
    m_fileDescriptor = open(m_journalPath.c_str(), O_RDWR | O_CREAT, 0644);
    if (m_fileDescriptor == -1) {
        return false;
    }
    
    // Get file size if it exists
    size_t fileSize = fileExists ? statBuf.st_size : 0;
    
    // Use initial size if file is empty or doesn't exist
    if (fileSize == 0) {
        fileSize = INITIAL_FILE_SIZE;
        
        // Resize the file
        if (ftruncate(m_fileDescriptor, fileSize) == -1) {
            close(m_fileDescriptor);
            m_fileDescriptor = -1;
            return false;
        }
    }
    
    // Map the file into memory
    m_mappedMemory = mmap(nullptr, fileSize, PROT_READ | PROT_WRITE, 
                        MAP_SHARED, m_fileDescriptor, 0);
    if (m_mappedMemory == MAP_FAILED) {
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
        m_mappedMemory = nullptr;
        return false;
    }
    
    // Store mapped size
    m_mappedSize = fileSize;
    
    // Determine write position by scanning existing entries
    if (fileExists) {
        size_t position = 0;
        uint64_t maxSequence = 0;
        
        while (position + sizeof(JournalEntryHeader) <= fileSize) {
            // Read header
            JournalEntryHeader header;
            std::memcpy(&header, static_cast<uint8_t*>(m_mappedMemory) + position, 
                      sizeof(JournalEntryHeader));
            
            // Validate header
            if (header.entrySize > MAX_FILE_SIZE) {
                // Invalid entry, stop scanning
                break;
            }
            
            // Check if entry fits in the file
            if (position + sizeof(JournalEntryHeader) + header.entrySize > fileSize) {
                // Partial entry, stop scanning
                break;
            }
            
            // Update position
            position += sizeof(JournalEntryHeader) + header.entrySize;
            
            // Update max sequence number
            if (header.sequenceNumber > maxSequence) {
                maxSequence = header.sequenceNumber;
            }
        }
        
        // Set write position
        m_writePosition.store(position, std::memory_order_release);
        
        // Set latest sequence number
        m_latestSequence.store(maxSequence, std::memory_order_release);
    } else {
        // New file, start at the beginning
        m_writePosition.store(0, std::memory_order_release);
        m_latestSequence.store(0, std::memory_order_release);
    }
    
    return true;
}

void Journal::unmapFile() {
    // Unmap the memory
    if (m_mappedMemory != nullptr) {
        munmap(m_mappedMemory, m_mappedSize);
        m_mappedMemory = nullptr;
    }
    
    // Close the file descriptor
    if (m_fileDescriptor != -1) {
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
    }
    
    m_mappedSize = 0;
}

bool Journal::resizeIfNeeded(size_t additionalSize) {
    // Calculate required size
    size_t writePosition = m_writePosition.load(std::memory_order_relaxed);
    size_t requiredSize = writePosition + additionalSize;
    
    // Check if we need to resize
    if (requiredSize <= m_mappedSize) {
        return true;
    }
    
    // Check if file would exceed maximum size
    if (requiredSize > MAX_FILE_SIZE) {
        return false;
    }
    
    // Calculate new size (round up to the nearest multiple of SIZE_INCREMENT)
    size_t newSize = ((requiredSize / SIZE_INCREMENT) + 1) * SIZE_INCREMENT;
    
    // On macOS, we need to unmap, resize, and remap
    // Unmap the current memory
    if (munmap(m_mappedMemory, m_mappedSize) != 0) {
        return false;
    }
    
    // Resize the file
    if (ftruncate(m_fileDescriptor, newSize) != 0) {
        return false;
    }
    
    // Remap the file
    void* newMemory = mmap(nullptr, newSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fileDescriptor, 0);
    if (newMemory == MAP_FAILED) {
        return false;
    }
    
    // Update member variables
    m_mappedMemory = newMemory;
    m_mappedSize = newSize;
    
    return true;
}

} // namespace journal
} // namespace persistence
} // namespace pinnacle