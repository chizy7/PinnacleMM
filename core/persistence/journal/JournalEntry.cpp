#include "JournalEntry.h"
#include "../../utils/TimeUtils.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace pinnacle {
namespace persistence {
namespace journal {

JournalEntry::JournalEntry(EntryType type, const std::vector<uint8_t>& data)
    : m_data(data) {
  m_header.sequenceNumber = 0; // Will be set by Journal
  m_header.timestamp = utils::TimeUtils::getCurrentNanos();
  m_header.type = type;
  m_header.entrySize = static_cast<uint32_t>(data.size());
  m_header.checksum = calculateChecksum();
}

JournalEntry JournalEntry::createOrderAddedEntry(const Order& order) {
  // Serialize order information
  std::ostringstream oss;
  oss << order.getOrderId() << "," << order.getSymbol() << ","
      << static_cast<int>(order.getSide()) << ","
      << static_cast<int>(order.getType()) << "," << std::fixed
      << std::setprecision(8) << order.getPrice() << "," << std::fixed
      << std::setprecision(8) << order.getQuantity() << ","
      << order.getTimestamp();

  std::string data = oss.str();
  std::vector<uint8_t> binaryData(data.begin(), data.end());

  return JournalEntry(EntryType::ORDER_ADDED, binaryData);
}

JournalEntry
JournalEntry::createOrderCanceledEntry(const std::string& orderId) {
  std::vector<uint8_t> binaryData(orderId.begin(), orderId.end());
  return JournalEntry(EntryType::ORDER_CANCELED, binaryData);
}

JournalEntry JournalEntry::createOrderExecutedEntry(const std::string& orderId,
                                                    double quantity) {
  std::ostringstream oss;
  oss << orderId << "," << std::fixed << std::setprecision(8) << quantity;

  std::string data = oss.str();
  std::vector<uint8_t> binaryData(data.begin(), data.end());

  return JournalEntry(EntryType::ORDER_EXECUTED, binaryData);
}

JournalEntry JournalEntry::createMarketOrderEntry(
    OrderSide side, double quantity,
    const std::vector<std::pair<std::string, double>>& fills) {
  std::ostringstream oss;
  oss << static_cast<int>(side) << "," << std::fixed << std::setprecision(8)
      << quantity << "," << fills.size();

  for (const auto& fill : fills) {
    oss << "," << fill.first << "," << std::fixed << std::setprecision(8)
        << fill.second;
  }

  std::string data = oss.str();
  std::vector<uint8_t> binaryData(data.begin(), data.end());

  return JournalEntry(EntryType::MARKET_ORDER_EXECUTED, binaryData);
}

JournalEntry JournalEntry::createCheckpointEntry(uint64_t snapshotId) {
  std::ostringstream oss;
  oss << snapshotId;

  std::string data = oss.str();
  std::vector<uint8_t> binaryData(data.begin(), data.end());

  return JournalEntry(EntryType::CHECKPOINT, binaryData);
}

bool JournalEntry::isValid() const {
  return m_header.checksum == calculateChecksum();
}

std::vector<uint8_t> JournalEntry::serialize() const {
  // Calculate total size
  size_t totalSize = sizeof(JournalEntryHeader) + m_data.size();

  // Create buffer
  std::vector<uint8_t> buffer(totalSize);

  // Copy header
  std::memcpy(buffer.data(), &m_header, sizeof(JournalEntryHeader));

  // Copy data
  if (!m_data.empty()) {
    std::memcpy(buffer.data() + sizeof(JournalEntryHeader), m_data.data(),
                m_data.size());
  }

  return buffer;
}

JournalEntry JournalEntry::deserialize(const uint8_t* data, size_t size) {
  if (size < sizeof(JournalEntryHeader)) {
    throw std::runtime_error("Invalid journal entry size");
  }

  // Extract header
  JournalEntryHeader header;
  std::memcpy(&header, data, sizeof(JournalEntryHeader));

  // Extract data
  std::vector<uint8_t> entryData;
  if (header.entrySize > 0) {
    if (size < sizeof(JournalEntryHeader) + header.entrySize) {
      throw std::runtime_error("Invalid journal entry size");
    }

    entryData.resize(header.entrySize);
    std::memcpy(entryData.data(), data + sizeof(JournalEntryHeader),
                header.entrySize);
  }

  // Create entry
  JournalEntry entry(header.type, entryData);
  entry.m_header =
      header; // Preserve original header (including sequence number)

  // Validate checksum
  if (!entry.isValid()) {
    throw std::runtime_error("Invalid journal entry checksum");
  }

  return entry;
}

uint32_t JournalEntry::calculateChecksum() const {
  // Simple checksum algorithm: sum of all bytes
  uint32_t checksum = 0;

  // Add header fields (excluding the checksum itself)
  checksum += static_cast<uint32_t>(m_header.sequenceNumber & 0xFFFFFFFF);
  checksum +=
      static_cast<uint32_t>((m_header.sequenceNumber >> 32) & 0xFFFFFFFF);
  checksum += static_cast<uint32_t>(m_header.timestamp & 0xFFFFFFFF);
  checksum += static_cast<uint32_t>((m_header.timestamp >> 32) & 0xFFFFFFFF);
  checksum += static_cast<uint32_t>(m_header.type);
  checksum += m_header.entrySize;

  // Add data bytes
  for (uint8_t byte : m_data) {
    checksum += byte;
  }

  return checksum;
}

} // namespace journal
} // namespace persistence
} // namespace pinnacle
