#pragma once

#include <atomic>
#include <memory>
#include <optional>

namespace pinnacle {
namespace utils {

/**
 * @class LockFreeQueue
 * @brief A lock-free single-producer single-consumer (SPSC) queue
 *
 * This implementation is optimized for ultra-low latency and is designed to be
 * used for passing messages between threads without locks or mutexes.
 *
 * @tparam T Type of elements stored in the queue
 * @tparam Capacity Fixed capacity of the queue
 */
template <typename T, size_t Capacity> class LockFreeQueue {
private:
  // Cache line size (assumed to be 64 bytes for most modern CPUs)
  static constexpr size_t CACHE_LINE_SIZE = 64;

  // Ensure the capacity is a power of 2 for efficient modulo operations
  static constexpr size_t CAPACITY = [] {
    size_t capacity = 1;
    while (capacity < Capacity) {
      capacity *= 2;
    }
    return capacity;
  }();

  // The mask for efficient modulo operations (CAPACITY - 1)
  static constexpr size_t MASK = CAPACITY - 1;

  // Storage for the queue elements
  std::array<T, CAPACITY> m_buffer;

  // Producer variables (aligned to cache line to prevent false sharing)
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_writeIndex{0};

  // Consumer variables (aligned to cache line to prevent false sharing)
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_readIndex{0};

public:
  /**
   * @brief Default constructor
   */
  LockFreeQueue() = default;

  /**
   * @brief Deleted copy constructor
   */
  LockFreeQueue(const LockFreeQueue&) = delete;

  /**
   * @brief Deleted copy assignment operator
   */
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  /**
   * @brief Deleted move constructor
   */
  LockFreeQueue(LockFreeQueue&&) = delete;

  /**
   * @brief Deleted move assignment operator
   */
  LockFreeQueue& operator=(LockFreeQueue&&) = delete;

  /**
   * @brief Try to enqueue an element
   *
   * @param item Element to enqueue
   * @return true if the element was enqueued, false if the queue was full
   */
  bool tryEnqueue(const T& item) {
    const size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
    const size_t nextWrite = (currentWrite + 1) & MASK;

    // Check if the queue is full
    if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
      return false;
    }

    m_buffer[currentWrite] = item;
    m_writeIndex.store(nextWrite, std::memory_order_release);
    return true;
  }

  /**
   * @brief Try to enqueue an element (move version)
   *
   * @param item Element to enqueue
   * @return true if the element was enqueued, false if the queue was full
   */
  bool tryEnqueue(T&& item) {
    const size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
    const size_t nextWrite = (currentWrite + 1) & MASK;

    // Check if the queue is full
    if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
      return false;
    }

    m_buffer[currentWrite] = std::move(item);
    m_writeIndex.store(nextWrite, std::memory_order_release);
    return true;
  }

  /**
   * @brief Try to dequeue an element
   *
   * @return The dequeued element, or std::nullopt if the queue was empty
   */
  std::optional<T> tryDequeue() {
    const size_t currentRead = m_readIndex.load(std::memory_order_relaxed);

    // Check if the queue is empty
    if (currentRead == m_writeIndex.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    T item = std::move(m_buffer[currentRead]);
    m_readIndex.store((currentRead + 1) & MASK, std::memory_order_release);
    return item;
  }

  /**
   * @brief Check if the queue is empty
   *
   * @return true if the queue is empty, false otherwise
   */
  bool isEmpty() const {
    return m_readIndex.load(std::memory_order_acquire) ==
           m_writeIndex.load(std::memory_order_acquire);
  }

  /**
   * @brief Get the current size of the queue
   *
   * Note: This is not thread-safe and should only be used for diagnostics
   *
   * @return Current number of elements in the queue
   */
  size_t size() const {
    const size_t writeIndex = m_writeIndex.load(std::memory_order_acquire);
    const size_t readIndex = m_readIndex.load(std::memory_order_acquire);

    if (writeIndex >= readIndex) {
      return writeIndex - readIndex;
    } else {
      return CAPACITY - (readIndex - writeIndex);
    }
  }

  /**
   * @brief Get the capacity of the queue
   *
   * @return Maximum number of elements the queue can hold
   */
  constexpr size_t capacity() const { return CAPACITY; }
};

/**
 * @class LockFreeMPMCQueue
 * @brief A lock-free multi-producer multi-consumer (MPMC) queue
 *
 * This implementation is based on the bounded MPMC queue by Dmitry Vyukov.
 * It provides thread-safe operations for multiple producers and consumers.
 *
 * @tparam T Type of elements stored in the queue
 * @tparam Capacity Fixed capacity of the queue (must be a power of 2)
 */
template <typename T, size_t Capacity> class LockFreeMPMCQueue {
private:
  // Ensure capacity is a power of 2
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  // Cache line size (assumed to be 64 bytes for most modern CPUs)
  static constexpr size_t CACHE_LINE_SIZE = 64;

  // Mask for efficient modulo operations
  static constexpr size_t MASK = Capacity - 1;

  // Cell structure for the queue
  struct Cell {
    std::atomic<size_t> sequence;
    T data;

    Cell() : sequence(0) {}
  };

  // Queue storage
  alignas(CACHE_LINE_SIZE) Cell m_buffer[Capacity];

  // Producer and consumer indices
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_enqueueIndex{0};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_dequeueIndex{0};

public:
  /**
   * @brief Constructor
   */
  LockFreeMPMCQueue() {
    // Initialize sequence numbers
    for (size_t i = 0; i < Capacity; ++i) {
      m_buffer[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  /**
   * @brief Deleted copy constructor
   */
  LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;

  /**
   * @brief Deleted assignment operator
   */
  LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

  /**
   * @brief Try to enqueue an element
   *
   * @param data Element to enqueue
   * @return true if the element was enqueued successfully, false if the queue
   * was full
   */
  bool tryEnqueue(const T& data) {
    Cell* cell;
    size_t pos;
    size_t seq;

    pos = m_enqueueIndex.load(std::memory_order_relaxed);

    for (;;) {
      cell = &m_buffer[pos & MASK];
      seq = cell->sequence.load(std::memory_order_acquire);

      // Calculate expected sequence for this position
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

      // If the cell is available for writing
      if (diff == 0) {
        // Try to claim this cell
        if (m_enqueueIndex.compare_exchange_weak(pos, pos + 1,
                                                 std::memory_order_relaxed)) {
          break;
        }
      }
      // If the queue is full (sequence is ahead of position)
      else if (diff < 0) {
        return false;
      }
      // Another thread claimed this position, try the next one
      else {
        pos = m_enqueueIndex.load(std::memory_order_relaxed);
      }
    }

    // Write the data to the claimed cell
    cell->data = data;

    // Update the sequence to indicate the cell is ready for reading
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }

  /**
   * @brief Try to enqueue an element (move version)
   *
   * @param data Element to enqueue
   * @return true if the element was enqueued successfully, false if the queue
   * was full
   */
  bool tryEnqueue(T&& data) {
    Cell* cell;
    size_t pos;
    size_t seq;

    pos = m_enqueueIndex.load(std::memory_order_relaxed);

    for (;;) {
      cell = &m_buffer[pos & MASK];
      seq = cell->sequence.load(std::memory_order_acquire);

      // Calculate expected sequence for this position
      intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

      // If the cell is available for writing
      if (diff == 0) {
        // Try to claim this cell
        if (m_enqueueIndex.compare_exchange_weak(pos, pos + 1,
                                                 std::memory_order_relaxed)) {
          break;
        }
      }
      // If the queue is full (sequence is ahead of position)
      else if (diff < 0) {
        return false;
      }
      // Another thread claimed this position, try the next one
      else {
        pos = m_enqueueIndex.load(std::memory_order_relaxed);
      }
    }

    // Write the data to the claimed cell
    cell->data = std::move(data);

    // Update the sequence to indicate the cell is ready for reading
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }

  /**
   * @brief Try to dequeue an element
   *
   * @param result Reference to store the dequeued element
   * @return true if an element was dequeued successfully, false if the queue
   * was empty
   */
  bool tryDequeue(T& result) {
    Cell* cell;
    size_t pos;
    size_t seq;

    pos = m_dequeueIndex.load(std::memory_order_relaxed);

    for (;;) {
      cell = &m_buffer[pos & MASK];
      seq = cell->sequence.load(std::memory_order_acquire);

      // Calculate expected sequence for this position
      intptr_t diff =
          static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

      // If the cell is available for reading
      if (diff == 0) {
        // Try to claim this cell
        if (m_dequeueIndex.compare_exchange_weak(pos, pos + 1,
                                                 std::memory_order_relaxed)) {
          break;
        }
      }
      // If the queue is empty (sequence is behind position)
      else if (diff < 0) {
        return false;
      }
      // Another thread claimed this position, try the next one
      else {
        pos = m_dequeueIndex.load(std::memory_order_relaxed);
      }
    }

    // Read the data from the claimed cell
    result = std::move(cell->data);

    // Update the sequence to indicate the cell is ready for writing
    cell->sequence.store(pos + Capacity, std::memory_order_release);

    return true;
  }

  /**
   * @brief Check if the queue is empty
   *
   * Note: This is not reliable in a multi-threaded environment and
   * should only be used for diagnostics.
   *
   * @return true if the queue is empty, false otherwise
   */
  bool isEmpty() const {
    return m_dequeueIndex.load(std::memory_order_relaxed) ==
           m_enqueueIndex.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get the approximate size of the queue
   *
   * Note: This is not reliable in a multi-threaded environment and
   * should only be used for diagnostics.
   *
   * @return Approximate number of elements in the queue
   */
  size_t approximateSize() const {
    size_t enqIdx = m_enqueueIndex.load(std::memory_order_relaxed);
    size_t deqIdx = m_dequeueIndex.load(std::memory_order_relaxed);
    return (enqIdx >= deqIdx) ? (enqIdx - deqIdx) : 0;
  }

  /**
   * @brief Get the capacity of the queue
   *
   * @return Maximum number of elements the queue can hold
   */
  constexpr size_t capacity() const { return Capacity; }
};

} // namespace utils
} // namespace pinnacle
