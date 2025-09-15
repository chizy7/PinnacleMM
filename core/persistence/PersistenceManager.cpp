#include "PersistenceManager.h"
#include "../utils/TimeUtils.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace pinnacle {
namespace persistence {

PersistenceManager& PersistenceManager::getInstance() {
  static PersistenceManager instance;
  return instance;
}

PersistenceManager::PersistenceManager() {
  // Default constructor, initialization happens in initialize()
}

PersistenceManager::~PersistenceManager() {
  // Clean up resources
  shutdown();
}

bool PersistenceManager::initialize(const std::string& dataDirectory) {
  m_dataDirectory = dataDirectory;

  // Create necessary directories
  if (!createDirectories()) {
    std::cerr << "Failed to create data directories: " << dataDirectory
              << std::endl;
    return false;
  }

  // Initialize is successful
  return true;
}

std::shared_ptr<journal::Journal>
PersistenceManager::getJournal(const std::string& symbol) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_journalsMutex);

  // Check if journal already exists
  auto it = m_journals.find(symbol);
  if (it != m_journals.end()) {
    return it->second;
  }

  // Create journal path
  std::string journalPath =
      m_dataDirectory + "/journals/" + symbol + ".journal";

  try {
    // Create a new journal
    auto journal = std::make_shared<journal::Journal>(journalPath);

    // Store in map
    m_journals[symbol] = journal;

    return journal;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create journal for " << symbol << ": " << e.what()
              << std::endl;
    return nullptr;
  }
}

std::shared_ptr<snapshot::SnapshotManager>
PersistenceManager::getSnapshotManager(const std::string& symbol) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_snapshotManagersMutex);

  // Check if snapshot manager already exists
  auto it = m_snapshotManagers.find(symbol);
  if (it != m_snapshotManagers.end()) {
    return it->second;
  }

  // Create snapshot directory
  std::string snapshotDirectory = m_dataDirectory + "/snapshots/" + symbol;

  try {
    // Create a new snapshot manager
    auto snapshotManager =
        std::make_shared<snapshot::SnapshotManager>(snapshotDirectory, symbol);

    // Store in map
    m_snapshotManagers[symbol] = snapshotManager;

    return snapshotManager;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create snapshot manager for " << symbol << ": "
              << e.what() << std::endl;
    return nullptr;
  }
}

bool PersistenceManager::recoverState() {
  // First, recover from snapshots (faster)
  bool snapshotRecovery = recoverFromSnapshots();

  // Then, apply any journal entries since last snapshot
  bool journalRecovery = recoverFromJournals();

  // Return success if either recovery method worked
  return snapshotRecovery || journalRecovery;
}

void PersistenceManager::performMaintenance() {
  // Get config values for maintenance
  int keepSnapshots = 5; // Default value, should be loaded from config
  uint64_t compactionThreshold =
      1000000; // Default value, should be loaded from config

  // Perform journal compaction
  for (const auto& pair : m_journals) {
    const std::string& symbol = pair.first;
    auto journal = pair.second;

    // Get snapshot manager
    auto snapshotManager = getSnapshotManager(symbol);
    if (!snapshotManager) {
      continue;
    }

    // Get latest snapshot ID
    uint64_t latestSnapshotId = snapshotManager->getLatestSnapshotId();
    if (latestSnapshotId == 0) {
      continue;
    }

    // Compact journal (remove entries before the latest snapshot)
    journal->compact(latestSnapshotId);

    // Cleanup old snapshots
    snapshotManager->cleanupOldSnapshots(keepSnapshots);
  }
}

void PersistenceManager::shutdown() {
  // Flush all journals
  for (const auto& pair : m_journals) {
    pair.second->flush();
  }

  // Clear maps
  m_journals.clear();
  m_snapshotManagers.clear();
}

bool PersistenceManager::createDirectories() {
  try {
    // Create base directory
    std::filesystem::create_directories(m_dataDirectory);

    // Create journals directory
    std::filesystem::create_directories(m_dataDirectory + "/journals");

    // Create snapshots directory
    std::filesystem::create_directories(m_dataDirectory + "/snapshots");

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create directories: " << e.what() << std::endl;
    return false;
  }
}

bool PersistenceManager::recoverFromJournals() {
  // This method would recover order books from journal entries
  // For each symbol, get the journal and replay entries
  // For now, I'll implement a placeholder that returns success

  // In a real implementation, this would:
  // 1. Get a list of all journals
  // 2. For each journal, get the associated order book
  // 3. Get the latest checkpoint sequence
  // 4. Read journal entries after the checkpoint
  // 5. Apply those entries to the order book

  return true;
}

bool PersistenceManager::recoverFromSnapshots() {
  // This method would recover order books from the latest snapshots
  // For each symbol, load the latest snapshot
  // For now, I'll implement a placeholder that returns success

  // In a real implementation, this would:
  // 1. Get a list of all snapshot managers
  // 2. For each manager, get the latest snapshot
  // 3. Load the snapshot into the order book

  return true;
}

} // namespace persistence
} // namespace pinnacle
