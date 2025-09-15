#pragma once

#include "../../orderbook/Order.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pinnacle {
namespace persistence {
namespace journal {

enum class EntryType : uint8_t {
  ORDER_ADDED = 1,
  ORDER_CANCELED = 2,
  ORDER_EXECUTED = 3,
  MARKET_ORDER_EXECUTED = 4,
  CHECKPOINT = 5
};

struct JournalEntryHeader {
  uint64_t sequenceNumber;
  uint64_t timestamp;
  EntryType type;
  uint32_t entrySize;
  uint32_t checksum;
};

class JournalEntry {
public:
  // Create entry for adding an order
  static JournalEntry createOrderAddedEntry(const Order& order);

  // Create entry for canceling an order
  static JournalEntry createOrderCanceledEntry(const std::string& orderId);

  // Create entry for executing an order
  static JournalEntry createOrderExecutedEntry(const std::string& orderId,
                                               double quantity);

  // Create entry for market order execution
  static JournalEntry createMarketOrderEntry(
      OrderSide side, double quantity,
      const std::vector<std::pair<std::string, double>>& fills);

  // Create checkpoint entry
  static JournalEntry createCheckpointEntry(uint64_t snapshotId);

  // Getters
  const JournalEntryHeader& getHeader() const { return m_header; }
  const std::vector<uint8_t>& getData() const { return m_data; }

  // Validate entry checksum
  bool isValid() const;

  // Serialize to binary
  std::vector<uint8_t> serialize() const;

  // Deserialize from binary
  static JournalEntry deserialize(const uint8_t* data, size_t size);

private:
  JournalEntryHeader m_header;
  std::vector<uint8_t> m_data;

  // Constructor
  JournalEntry(EntryType type, const std::vector<uint8_t>& data);

  // Assign sequence number (done by Journal when adding entries)
  void setSequenceNumber(uint64_t seq) { m_header.sequenceNumber = seq; }

  // Calculate checksum
  uint32_t calculateChecksum() const;

  // Friend class declarations
  friend class Journal;
};

} // namespace journal
} // namespace persistence
} // namespace pinnacle
