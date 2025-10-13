#pragma once

#include "../orderbook/OrderBook.h"
#include "journal/Journal.h"
#include "snapshot/SnapshotManager.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace persistence {

class PersistenceManager {
public:
  // Singleton instance
  static PersistenceManager& getInstance();

  // Initialize persistence with data directory
  bool initialize(const std::string& dataDirectory);

  // Get journal for a specific symbol
  std::shared_ptr<journal::Journal> getJournal(const std::string& symbol);

  // Get snapshot manager for a specific symbol
  std::shared_ptr<snapshot::SnapshotManager>
  getSnapshotManager(const std::string& symbol);

  // Recover state after restart
  bool recoverState();

  // Get a recovered order book for a specific symbol
  std::shared_ptr<OrderBook> getRecoveredOrderBook(const std::string& symbol);

  // Get all recovered order books
  std::unordered_map<std::string, std::shared_ptr<OrderBook>>
  getAllRecoveredOrderBooks();

  // Check if order books have been recovered
  bool hasRecoveredOrderBooks() const;

  // Clear recovered order books (e.g., after they've been used)
  void clearRecoveredOrderBooks();

  // Periodic housekeeping (e.g., compaction)
  void performMaintenance();

  // Clean shutdown
  void shutdown();

private:
  PersistenceManager();
  ~PersistenceManager();

  // Non-copyable
  PersistenceManager(const PersistenceManager&) = delete;
  PersistenceManager& operator=(const PersistenceManager&) = delete;

  // Implementation details
  std::string m_dataDirectory;
  std::unordered_map<std::string, std::shared_ptr<journal::Journal>> m_journals;
  std::unordered_map<std::string, std::shared_ptr<snapshot::SnapshotManager>>
      m_snapshotManagers;
  std::unordered_map<std::string, std::shared_ptr<OrderBook>>
      m_recoveredOrderBooks;
  std::mutex m_journalsMutex;
  std::mutex m_snapshotManagersMutex;
  mutable std::mutex m_recoveredOrderBooksMutex;

  // Create necessary directories
  bool createDirectories();

  // Internal recovery logic
  bool recoverFromJournals();
  bool recoverFromSnapshots();
};

} // namespace persistence
} // namespace pinnacle
