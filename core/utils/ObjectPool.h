#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace pinnacle {
namespace utils {

/**
 * @class ObjectPool
 * @brief Thread-safe object pool template with custom deleter recycling
 *
 * Pre-allocates objects and returns them via shared_ptr with a custom deleter
 * that recycles objects back to the pool instead of destroying them.
 *
 * @tparam T The type of object to pool
 */
template <typename T> class ObjectPool {
public:
  /**
   * @brief Construct pool with initial capacity
   * @param initialSize Number of objects to pre-allocate
   */
  explicit ObjectPool(size_t initialSize = 64) {
    m_pool.reserve(initialSize);
    for (size_t i = 0; i < initialSize; ++i) {
      m_pool.push_back(std::make_unique<T>());
    }
    m_totalAllocated.store(initialSize, std::memory_order_relaxed);
  }

  /**
   * @brief Construct pool with a factory function
   * @param initialSize Number of objects to pre-allocate
   * @param factory Function to create new objects
   */
  ObjectPool(size_t initialSize, std::function<std::unique_ptr<T>()> factory)
      : m_factory(std::move(factory)) {
    m_pool.reserve(initialSize);
    for (size_t i = 0; i < initialSize; ++i) {
      m_pool.push_back(m_factory ? m_factory() : std::make_unique<T>());
    }
    m_totalAllocated.store(initialSize, std::memory_order_relaxed);
  }

  ~ObjectPool() { m_alive->store(false, std::memory_order_release); }

  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;

  /**
   * @brief Acquire an object from the pool
   *
   * Returns a shared_ptr with a custom deleter that recycles the object back
   * to the pool when the last reference is released.
   *
   * @return shared_ptr to a pooled object
   */
  std::shared_ptr<T> acquire() {
    T* raw = nullptr;

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_pool.empty()) {
        raw = m_pool.back().release();
        m_pool.pop_back();
      }
    }

    if (!raw) {
      // Pool exhausted — allocate a new object
      raw = m_factory ? m_factory().release() : new T();
      m_totalAllocated.fetch_add(1, std::memory_order_relaxed);
    }

    m_acquireCount.fetch_add(1, std::memory_order_relaxed);

    // Return with custom deleter that recycles back to pool (or deletes if pool
    // is destroyed)
    auto alive = m_alive;
    return std::shared_ptr<T>(raw, [this, alive](T* obj) {
      if (alive->load(std::memory_order_acquire)) {
        recycle(obj);
      } else {
        delete obj;
      }
    });
  }

  /**
   * @brief Get current number of available objects in the pool
   */
  size_t available() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pool.size();
  }

  /**
   * @brief Get total number of objects ever allocated by this pool
   */
  size_t totalAllocated() const {
    return m_totalAllocated.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get total number of acquire() calls
   */
  size_t acquireCount() const {
    return m_acquireCount.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get total number of recycle operations
   */
  size_t recycleCount() const {
    return m_recycleCount.load(std::memory_order_relaxed);
  }

private:
  void recycle(T* obj) {
    if (!obj) {
      return;
    }

    m_recycleCount.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.push_back(std::unique_ptr<T>(obj));
  }

  mutable std::mutex m_mutex;
  std::vector<std::unique_ptr<T>> m_pool;
  std::function<std::unique_ptr<T>()> m_factory;

  std::shared_ptr<std::atomic<bool>> m_alive =
      std::make_shared<std::atomic<bool>>(true);
  std::atomic<size_t> m_totalAllocated{0};
  std::atomic<size_t> m_acquireCount{0};
  std::atomic<size_t> m_recycleCount{0};
};

} // namespace utils
} // namespace pinnacle
