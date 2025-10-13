#include "PersistenceManager.h"
#include "../utils/TimeUtils.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>

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
    spdlog::error("Failed to create data directories: {}", dataDirectory);
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
    spdlog::error("Failed to create journal for {}: {}", symbol, e.what());
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
    spdlog::error("Failed to create snapshot manager for {}: {}", symbol,
                  e.what());
    return nullptr;
  }
}

RecoveryStatus PersistenceManager::recoverState() {
  // First, recover from snapshots (faster)
  RecoveryStatus snapshotStatus = recoverFromSnapshots();

  // Then, apply any journal entries since last snapshot
  RecoveryStatus journalStatus = recoverFromJournals();

  // Determine overall recovery status
  // If either recovery failed with errors, report failure
  if (snapshotStatus == RecoveryStatus::Failed ||
      journalStatus == RecoveryStatus::Failed) {
    spdlog::error(
        "Recovery failed - encountered errors during recovery process");
    return RecoveryStatus::Failed;
  }

  // If either recovery succeeded, overall recovery is successful
  if (snapshotStatus == RecoveryStatus::Success ||
      journalStatus == RecoveryStatus::Success) {
    return RecoveryStatus::Success;
  }

  // Both returned CleanStart - this is a clean start
  return RecoveryStatus::CleanStart;
}

void PersistenceManager::performMaintenance() {
  spdlog::info("Starting persistence maintenance...");

  // Configuration values for maintenance
  // TODO: Load these from a PersistenceConfig file when available
  const int keepSnapshots = 5; // Keep the 5 most recent snapshots
  const uint64_t compactionThreshold =
      1000000; // Compact if journal > 1M entries

  int journalsCompacted = 0;
  int snapshotsCleaned = 0;

  try {
    // Lock to prevent concurrent modifications
    std::lock_guard<std::mutex> journalLock(m_journalsMutex);
    std::lock_guard<std::mutex> snapshotLock(m_snapshotManagersMutex);

    // Perform maintenance on each symbol
    for (const auto& pair : m_journals) {
      const std::string& symbol = pair.first;
      auto journal = pair.second;

      if (!journal) {
        continue;
      }

      // Get snapshot manager for this symbol (already holding
      // m_snapshotManagersMutex)
      auto snapshotIt = m_snapshotManagers.find(symbol);
      if (snapshotIt == m_snapshotManagers.end() || !snapshotIt->second) {
        spdlog::warn("No snapshot manager found for symbol: {}", symbol);
        continue;
      }
      auto snapshotManager = snapshotIt->second;

      // Get the latest snapshot ID (used as checkpoint for compaction)
      uint64_t latestSnapshotId = snapshotManager->getLatestSnapshotId();

      // Perform journal compaction if we have a valid checkpoint
      if (latestSnapshotId > 0) {
        uint64_t currentSequence = journal->getLatestSequenceNumber();

        // Only compact if the journal has grown beyond the threshold
        // Guard against unsigned underflow by checking currentSequence >
        // latestSnapshotId first
        if (currentSequence > latestSnapshotId &&
            (currentSequence - latestSnapshotId) > compactionThreshold) {
          spdlog::info("Compacting journal for symbol: {} (sequence: {} -> {})",
                       symbol, latestSnapshotId, currentSequence);

          if (journal->compact(latestSnapshotId)) {
            journalsCompacted++;
            spdlog::info("Successfully compacted journal for symbol: {}",
                         symbol);
          } else {
            spdlog::error("Failed to compact journal for symbol: {}", symbol);
          }
        } else {
          spdlog::debug("Journal for {} does not need compaction (entries: {})",
                        symbol,
                        currentSequence > latestSnapshotId
                            ? currentSequence - latestSnapshotId
                            : 0);
        }
      } else {
        spdlog::debug("No checkpoint found for symbol: {}, skipping compaction",
                      symbol);
      }

      // Cleanup old snapshots (keep only the N most recent)
      if (snapshotManager->cleanupOldSnapshots(keepSnapshots)) {
        snapshotsCleaned++;
        spdlog::info("Cleaned up old snapshots for symbol: {}", symbol);
      }
    }

    // Also check for snapshot managers without journals
    for (const auto& pair : m_snapshotManagers) {
      const std::string& symbol = pair.first;
      auto snapshotManager = pair.second;

      // Skip if we already processed this symbol
      if (m_journals.find(symbol) != m_journals.end()) {
        continue;
      }

      // Cleanup old snapshots
      if (snapshotManager->cleanupOldSnapshots(keepSnapshots)) {
        snapshotsCleaned++;
        spdlog::info("Cleaned up old snapshots for symbol: {}", symbol);
      }
    }

    spdlog::info(
        "Maintenance completed: {} journals compacted, {} snapshots cleaned",
        journalsCompacted, snapshotsCleaned);

  } catch (const std::exception& e) {
    spdlog::error("Maintenance failed: {}", e.what());
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
    spdlog::error("Failed to create directories: {}", e.what());
    return false;
  }
}

RecoveryStatus PersistenceManager::recoverFromJournals() {
  spdlog::info("Starting journal recovery...");

  try {
    // Get the journals directory path
    std::string journalsDir = m_dataDirectory + "/journals";

    // Check if journals directory exists
    if (!std::filesystem::exists(journalsDir)) {
      spdlog::info("Journals directory does not exist: {} (clean start)",
                   journalsDir);
      return RecoveryStatus::CleanStart;
    }

    // Enumerate all journal files
    int recoveredCount = 0;
    bool hadErrors = false;
    for (const auto& entry : std::filesystem::directory_iterator(journalsDir)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      std::string filename = entry.path().filename().string();

      // Check if this is a journal file (ends with .journal)
      if (filename.find(".journal") != filename.length() - 8) {
        continue;
      }

      // Extract symbol from filename (e.g., "BTC-USD.journal" -> "BTC-USD")
      std::string symbol = filename.substr(0, filename.length() - 8);

      // Get or create journal for this symbol
      auto journal = getJournal(symbol);
      if (!journal) {
        spdlog::error("Failed to get journal for symbol: {}", symbol);
        hadErrors = true;
        continue;
      }

      // Get the snapshot manager to find the latest checkpoint
      auto snapshotManager = getSnapshotManager(symbol);
      uint64_t checkpointSequence = 0;

      if (snapshotManager) {
        // Get the latest snapshot ID to use as checkpoint
        checkpointSequence = snapshotManager->getLatestSnapshotId();
      }

      // Check if we already have a recovered order book from snapshot recovery
      std::shared_ptr<OrderBook> orderBook;
      {
        std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
        auto it = m_recoveredOrderBooks.find(symbol);
        if (it != m_recoveredOrderBooks.end()) {
          orderBook = it->second;
          spdlog::info("Using existing recovered order book for {}", symbol);
        }
      }

      // If no existing order book, try to load from snapshot or create new one
      if (!orderBook) {
        if (snapshotManager && checkpointSequence > 0) {
          auto snapshotOrderBook = snapshotManager->loadLatestSnapshot();
          if (snapshotOrderBook) {
            orderBook = snapshotOrderBook;
            spdlog::info("Loaded snapshot for {}, replaying journal entries "
                         "after checkpoint {}",
                         symbol, checkpointSequence);
          }
        }

        // If still no order book, create a new one
        if (!orderBook) {
          orderBook = std::make_shared<OrderBook>(
              symbol, false); // Disable persistence for recovery
          spdlog::info("Created new order book for {} journal recovery",
                       symbol);
        }
      }

      // Read journal entries after the checkpoint
      auto entries = journal->readEntriesAfter(checkpointSequence);

      if (entries.empty()) {
        spdlog::info("No journal entries to replay for symbol: {}", symbol);
        // Still store the order book if we loaded it from snapshot
        if (orderBook) {
          std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
          m_recoveredOrderBooks[symbol] = orderBook;
        }
        continue;
      }

      // Replay the journal entries
      if (orderBook->recoverFromJournal(journal)) {
        spdlog::info("Successfully recovered journal for symbol: {} ({} "
                     "entries replayed)",
                     symbol, entries.size());

        // Store the recovered order book
        {
          std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
          m_recoveredOrderBooks[symbol] = orderBook;
        }

        recoveredCount++;
      } else {
        spdlog::error("Failed to recover journal for symbol: {}", symbol);
        hadErrors = true;
      }
    }

    // Determine return status
    if (hadErrors) {
      spdlog::error("Journal recovery completed with errors");
      return RecoveryStatus::Failed;
    }

    if (recoveredCount > 0) {
      spdlog::info("Journal recovery completed: {} symbols recovered",
                   recoveredCount);
      return RecoveryStatus::Success;
    } else {
      spdlog::info("No journals found to recover (clean start)");
      return RecoveryStatus::CleanStart;
    }
  } catch (const std::exception& e) {
    spdlog::error("Journal recovery failed with exception: {}", e.what());
    return RecoveryStatus::Failed;
  }
}

RecoveryStatus PersistenceManager::recoverFromSnapshots() {
  spdlog::info("Starting snapshot recovery...");

  try {
    // Get the snapshots directory path
    std::string snapshotsDir = m_dataDirectory + "/snapshots";

    // Check if snapshots directory exists
    if (!std::filesystem::exists(snapshotsDir)) {
      spdlog::info("Snapshots directory does not exist: {} (clean start)",
                   snapshotsDir);
      return RecoveryStatus::CleanStart;
    }

    // Enumerate all subdirectories (one per symbol)
    int recoveredCount = 0;
    bool hadErrors = false;
    for (const auto& entry :
         std::filesystem::directory_iterator(snapshotsDir)) {
      if (!entry.is_directory()) {
        continue;
      }

      // Extract symbol from directory name
      std::string symbol = entry.path().filename().string();

      // Get or create snapshot manager for this symbol
      auto snapshotManager = getSnapshotManager(symbol);
      if (!snapshotManager) {
        spdlog::error("Failed to create snapshot manager for symbol: {}",
                      symbol);
        hadErrors = true;
        continue;
      }

      // Load the latest snapshot
      auto orderBook = snapshotManager->loadLatestSnapshot();
      if (!orderBook) {
        spdlog::warn("No valid snapshot found for symbol: {}", symbol);
        // This is a warning, not an error - could be corrupt or empty snapshot
        continue;
      }

      // Store the recovered order book
      {
        std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
        m_recoveredOrderBooks[symbol] = orderBook;
      }

      spdlog::info(
          "Successfully recovered snapshot for symbol: {} with {} orders",
          symbol, orderBook->getOrderCount());
      recoveredCount++;
    }

    // Determine return status
    if (hadErrors) {
      spdlog::error("Snapshot recovery completed with errors");
      return RecoveryStatus::Failed;
    }

    if (recoveredCount > 0) {
      spdlog::info("Snapshot recovery completed: {} symbols recovered",
                   recoveredCount);
      return RecoveryStatus::Success;
    } else {
      spdlog::info("No snapshots found to recover (clean start)");
      return RecoveryStatus::CleanStart;
    }
  } catch (const std::exception& e) {
    spdlog::error("Snapshot recovery failed with exception: {}", e.what());
    return RecoveryStatus::Failed;
  }
}

std::shared_ptr<OrderBook>
PersistenceManager::getRecoveredOrderBook(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
  auto it = m_recoveredOrderBooks.find(symbol);
  if (it != m_recoveredOrderBooks.end()) {
    return it->second;
  }
  return nullptr;
}

std::unordered_map<std::string, std::shared_ptr<OrderBook>>
PersistenceManager::getAllRecoveredOrderBooks() {
  std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
  return m_recoveredOrderBooks;
}

bool PersistenceManager::hasRecoveredOrderBooks() const {
  std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
  return !m_recoveredOrderBooks.empty();
}

void PersistenceManager::clearRecoveredOrderBooks() {
  std::lock_guard<std::mutex> lock(m_recoveredOrderBooksMutex);
  m_recoveredOrderBooks.clear();
  spdlog::info("Cleared all recovered order books");
}

} // namespace persistence
} // namespace pinnacle
