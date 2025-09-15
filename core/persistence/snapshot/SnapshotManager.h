#pragma once

#include "../../orderbook/OrderBook.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace persistence {
namespace snapshot {

class SnapshotManager {
public:
  // Constructor with snapshot directory
  explicit SnapshotManager(const std::string& snapshotDirectory,
                           const std::string& symbol);
  ~SnapshotManager();

  // Create snapshot from order book
  uint64_t createSnapshot(const OrderBook& orderBook);

  // Load latest snapshot
  std::shared_ptr<OrderBook> loadLatestSnapshot();

  // Load specific snapshot
  std::shared_ptr<OrderBook> loadSnapshot(uint64_t snapshotId);

  // Get latest snapshot ID
  uint64_t getLatestSnapshotId() const;

  // Remove old snapshots
  bool cleanupOldSnapshots(int keepCount);

private:
  // Snapshot directory
  std::string m_snapshotDirectory;

  // Symbol
  std::string m_symbol;

  // Latest snapshot ID
  std::atomic<uint64_t> m_latestSnapshotId{0};

  // Mutex for snapshot operations
  std::mutex m_snapshotMutex;

  // Get snapshot file path
  std::string getSnapshotPath(uint64_t snapshotId) const;

  // List all available snapshots
  std::vector<uint64_t> listSnapshots() const;

  // Memory-map operations
  bool writeSnapshotToFile(uint64_t snapshotId, const OrderBook& orderBook);
  std::shared_ptr<OrderBook> readSnapshotFromFile(uint64_t snapshotId);
};

} // namespace snapshot
} // namespace persistence
} // namespace pinnacle
