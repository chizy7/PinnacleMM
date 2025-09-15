#include "SnapshotManager.h"
#include "../../utils/TimeUtils.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pinnacle {
namespace persistence {
namespace snapshot {

SnapshotManager::SnapshotManager(const std::string& snapshotDirectory,
                                 const std::string& symbol)
    : m_snapshotDirectory(snapshotDirectory), m_symbol(symbol) {
  // Create directory if it doesn't exist
  std::filesystem::create_directories(snapshotDirectory);

  // Find the latest snapshot ID
  auto snapshots = listSnapshots();
  if (!snapshots.empty()) {
    m_latestSnapshotId.store(
        *std::max_element(snapshots.begin(), snapshots.end()),
        std::memory_order_release);
  }
}

SnapshotManager::~SnapshotManager() {
  // No resources to clean up
}

uint64_t SnapshotManager::createSnapshot(const OrderBook& orderBook) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_snapshotMutex);

  // Generate a new snapshot ID
  uint64_t snapshotId = utils::TimeUtils::getCurrentNanos();

  // Write snapshot to file
  if (!writeSnapshotToFile(snapshotId, orderBook)) {
    return 0; // Failed
  }

  // Update latest snapshot ID
  m_latestSnapshotId.store(snapshotId, std::memory_order_release);

  return snapshotId;
}

std::shared_ptr<OrderBook> SnapshotManager::loadLatestSnapshot() {
  // Get the latest snapshot ID
  uint64_t latestId = m_latestSnapshotId.load(std::memory_order_acquire);
  if (latestId == 0) {
    return nullptr; // No snapshots available
  }

  // Load the snapshot
  return loadSnapshot(latestId);
}

std::shared_ptr<OrderBook> SnapshotManager::loadSnapshot(uint64_t snapshotId) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_snapshotMutex);

  // Read snapshot from file
  return readSnapshotFromFile(snapshotId);
}

uint64_t SnapshotManager::getLatestSnapshotId() const {
  return m_latestSnapshotId.load(std::memory_order_acquire);
}

bool SnapshotManager::cleanupOldSnapshots(int keepCount) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_snapshotMutex);

  // Get all snapshots
  auto snapshots = listSnapshots();

  // If we have fewer than keepCount, nothing to do
  if (static_cast<int>(snapshots.size()) <= keepCount) {
    return true;
  }

  // Sort snapshots in descending order
  std::sort(snapshots.begin(), snapshots.end(), std::greater<uint64_t>());

  // Delete all but the latest keepCount snapshots
  bool success = true;
  for (size_t i = keepCount; i < snapshots.size(); ++i) {
    std::string path = getSnapshotPath(snapshots[i]);

    try {
      std::filesystem::remove(path);
    } catch (const std::exception& e) {
      std::cerr << "Failed to delete snapshot " << path << ": " << e.what()
                << std::endl;
      success = false;
    }
  }

  return success;
}

std::string SnapshotManager::getSnapshotPath(uint64_t snapshotId) const {
  std::ostringstream oss;
  oss << m_snapshotDirectory << "/" << m_symbol << "-" << snapshotId
      << ".snapshot";
  return oss.str();
}

std::vector<uint64_t> SnapshotManager::listSnapshots() const {
  std::vector<uint64_t> snapshots;

  // Get all files in the snapshot directory
  for (const auto& entry :
       std::filesystem::directory_iterator(m_snapshotDirectory)) {
    std::string filename = entry.path().filename().string();

    // Check if this is a snapshot file for our symbol
    std::string prefix = m_symbol + "-";
    std::string suffix = ".snapshot";

    if (filename.find(prefix) == 0 &&
        filename.find(suffix) == filename.length() - suffix.length()) {
      // Extract the snapshot ID
      std::string idStr =
          filename.substr(prefix.length(), filename.length() - prefix.length() -
                                               suffix.length());

      try {
        uint64_t id = std::stoull(idStr);
        snapshots.push_back(id);
      } catch (const std::exception& e) {
        // Ignore invalid filenames
      }
    }
  }

  return snapshots;
}

bool SnapshotManager::writeSnapshotToFile(uint64_t snapshotId,
                                          const OrderBook& orderBook) {
  // Get snapshot file path
  std::string path = getSnapshotPath(snapshotId);

  // For now, I'll use a simple serialization approach
  // In a full implementation, I'd use memory mapping and a more efficient
  // format

  try {
    // Create a temporary file
    std::string tempPath = path + ".tmp";
    std::ofstream file(tempPath, std::ios::binary);

    if (!file) {
      return false;
    }

    // Get order book snapshot
    auto snapshot = orderBook.getSnapshot();

    // Write symbol
    std::string symbol = snapshot->getSymbol();
    size_t symbolLength = symbol.length();
    file.write(reinterpret_cast<const char*>(&symbolLength),
               sizeof(symbolLength));
    file.write(symbol.c_str(), symbolLength);

    // Write timestamp
    uint64_t timestamp = snapshot->getTimestamp();
    file.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));

    // Write bids
    const auto& bids = snapshot->getBids();
    size_t bidCount = bids.size();
    file.write(reinterpret_cast<const char*>(&bidCount), sizeof(bidCount));

    for (const auto& level : bids) {
      // Write price
      file.write(reinterpret_cast<const char*>(&level.price),
                 sizeof(level.price));

      // Write total quantity
      file.write(reinterpret_cast<const char*>(&level.totalQuantity),
                 sizeof(level.totalQuantity));

      // Write order count
      size_t orderCount = level.orders.size();
      file.write(reinterpret_cast<const char*>(&orderCount),
                 sizeof(orderCount));

      // Write each order
      for (const auto& order : level.orders) {
        // Write order ID
        std::string orderId = order->getOrderId();
        size_t orderIdLength = orderId.length();
        file.write(reinterpret_cast<const char*>(&orderIdLength),
                   sizeof(orderIdLength));
        file.write(orderId.c_str(), orderIdLength);

        // Write order side
        OrderSide side = order->getSide();
        file.write(reinterpret_cast<const char*>(&side), sizeof(side));

        // Write order type
        OrderType type = order->getType();
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));

        // Write order price
        double price = order->getPrice();
        file.write(reinterpret_cast<const char*>(&price), sizeof(price));

        // Write order quantity
        double quantity = order->getQuantity();
        file.write(reinterpret_cast<const char*>(&quantity), sizeof(quantity));

        // Write order filled quantity
        double filledQuantity = order->getFilledQuantity();
        file.write(reinterpret_cast<const char*>(&filledQuantity),
                   sizeof(filledQuantity));

        // Write order timestamp
        uint64_t orderTimestamp = order->getTimestamp();
        file.write(reinterpret_cast<const char*>(&orderTimestamp),
                   sizeof(orderTimestamp));
      }
    }

    // Write asks
    const auto& asks = snapshot->getAsks();
    size_t askCount = asks.size();
    file.write(reinterpret_cast<const char*>(&askCount), sizeof(askCount));

    for (const auto& level : asks) {
      // Write price
      file.write(reinterpret_cast<const char*>(&level.price),
                 sizeof(level.price));

      // Write total quantity
      file.write(reinterpret_cast<const char*>(&level.totalQuantity),
                 sizeof(level.totalQuantity));

      // Write order count
      size_t orderCount = level.orders.size();
      file.write(reinterpret_cast<const char*>(&orderCount),
                 sizeof(orderCount));

      // Write each order
      for (const auto& order : level.orders) {
        // Write order ID
        std::string orderId = order->getOrderId();
        size_t orderIdLength = orderId.length();
        file.write(reinterpret_cast<const char*>(&orderIdLength),
                   sizeof(orderIdLength));
        file.write(orderId.c_str(), orderIdLength);

        // Write order side
        OrderSide side = order->getSide();
        file.write(reinterpret_cast<const char*>(&side), sizeof(side));

        // Write order type
        OrderType type = order->getType();
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));

        // Write order price
        double price = order->getPrice();
        file.write(reinterpret_cast<const char*>(&price), sizeof(price));

        // Write order quantity
        double quantity = order->getQuantity();
        file.write(reinterpret_cast<const char*>(&quantity), sizeof(quantity));

        // Write order filled quantity
        double filledQuantity = order->getFilledQuantity();
        file.write(reinterpret_cast<const char*>(&filledQuantity),
                   sizeof(filledQuantity));

        // Write order timestamp
        uint64_t orderTimestamp = order->getTimestamp();
        file.write(reinterpret_cast<const char*>(&orderTimestamp),
                   sizeof(orderTimestamp));
      }
    }

    // Flush and close
    file.flush();
    file.close();

    // Rename temporary file to final file
    std::filesystem::rename(tempPath, path);

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to write snapshot: " << e.what() << std::endl;
    return false;
  }
}

std::shared_ptr<OrderBook>
SnapshotManager::readSnapshotFromFile(uint64_t snapshotId) {
  // Get snapshot file path
  std::string path = getSnapshotPath(snapshotId);

  // Check if file exists
  if (!std::filesystem::exists(path)) {
    return nullptr;
  }

  try {
    // Open the file for reading
    std::ifstream file(path, std::ios::binary);

    if (!file) {
      return nullptr;
    }

    // Read symbol
    size_t symbolLength;
    file.read(reinterpret_cast<char*>(&symbolLength), sizeof(symbolLength));

    std::string symbol(symbolLength, ' ');
    file.read(&symbol[0], symbolLength);

    // Create a new order book
    auto orderBook = std::make_shared<OrderBook>(symbol);

    // Read timestamp (skip for now)
    uint64_t timestamp;
    file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));

    // Read bids
    size_t bidCount;
    file.read(reinterpret_cast<char*>(&bidCount), sizeof(bidCount));

    for (size_t i = 0; i < bidCount; ++i) {
      // Read price
      double price;
      file.read(reinterpret_cast<char*>(&price), sizeof(price));

      // Read total quantity (skip for now)
      double totalQuantity;
      file.read(reinterpret_cast<char*>(&totalQuantity), sizeof(totalQuantity));

      // Read order count
      size_t orderCount;
      file.read(reinterpret_cast<char*>(&orderCount), sizeof(orderCount));

      // Read each order
      for (size_t j = 0; j < orderCount; ++j) {
        // Read order ID
        size_t orderIdLength;
        file.read(reinterpret_cast<char*>(&orderIdLength),
                  sizeof(orderIdLength));

        std::string orderId(orderIdLength, ' ');
        file.read(&orderId[0], orderIdLength);

        // Read order side
        OrderSide side;
        file.read(reinterpret_cast<char*>(&side), sizeof(side));

        // Read order type
        OrderType type;
        file.read(reinterpret_cast<char*>(&type), sizeof(type));

        // Read order price
        double orderPrice;
        file.read(reinterpret_cast<char*>(&orderPrice), sizeof(orderPrice));

        // Read order quantity
        double quantity;
        file.read(reinterpret_cast<char*>(&quantity), sizeof(quantity));

        // Read order filled quantity
        double filledQuantity;
        file.read(reinterpret_cast<char*>(&filledQuantity),
                  sizeof(filledQuantity));

        // Read order timestamp
        uint64_t orderTimestamp;
        file.read(reinterpret_cast<char*>(&orderTimestamp),
                  sizeof(orderTimestamp));

        // Create and add the order
        auto order = std::make_shared<Order>(
            orderId, symbol, side, type, orderPrice, quantity, orderTimestamp);

        // Apply filled quantity if any
        if (filledQuantity > 0) {
          order->fill(filledQuantity, utils::TimeUtils::getCurrentNanos());
        }

        // Add to order book
        orderBook->addOrder(order);
      }
    }

    // Read asks
    size_t askCount;
    file.read(reinterpret_cast<char*>(&askCount), sizeof(askCount));

    for (size_t i = 0; i < askCount; ++i) {
      // Read price
      double price;
      file.read(reinterpret_cast<char*>(&price), sizeof(price));

      // Read total quantity (skip for now)
      double totalQuantity;
      file.read(reinterpret_cast<char*>(&totalQuantity), sizeof(totalQuantity));

      // Read order count
      size_t orderCount;
      file.read(reinterpret_cast<char*>(&orderCount), sizeof(orderCount));

      // Read each order
      for (size_t j = 0; j < orderCount; ++j) {
        // Read order ID
        size_t orderIdLength;
        file.read(reinterpret_cast<char*>(&orderIdLength),
                  sizeof(orderIdLength));

        std::string orderId(orderIdLength, ' ');
        file.read(&orderId[0], orderIdLength);

        // Read order side
        OrderSide side;
        file.read(reinterpret_cast<char*>(&side), sizeof(side));

        // Read order type
        OrderType type;
        file.read(reinterpret_cast<char*>(&type), sizeof(type));

        // Read order price
        double orderPrice;
        file.read(reinterpret_cast<char*>(&orderPrice), sizeof(orderPrice));

        // Read order quantity
        double quantity;
        file.read(reinterpret_cast<char*>(&quantity), sizeof(quantity));

        // Read order filled quantity
        double filledQuantity;
        file.read(reinterpret_cast<char*>(&filledQuantity),
                  sizeof(filledQuantity));

        // Read order timestamp
        uint64_t orderTimestamp;
        file.read(reinterpret_cast<char*>(&orderTimestamp),
                  sizeof(orderTimestamp));

        // Create and add the order
        auto order = std::make_shared<Order>(
            orderId, symbol, side, type, orderPrice, quantity, orderTimestamp);

        // Apply filled quantity if any
        if (filledQuantity > 0) {
          order->fill(filledQuantity, utils::TimeUtils::getCurrentNanos());
        }

        // Add to order book
        orderBook->addOrder(order);
      }
    }

    return orderBook;
  } catch (const std::exception& e) {
    std::cerr << "Failed to read snapshot: " << e.what() << std::endl;
    return nullptr;
  }
}

} // namespace snapshot
} // namespace persistence
} // namespace pinnacle
