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
 * Uses a SharedState pattern so that deleters safely handle the case where
 * the pool is destroyed before all objects are returned.
 *
 * @tparam T The type of object to pool
 */
template <typename T> class ObjectPool {
public:
  /**
   * @brief Construct pool with initial capacity
   * @param initialSize Number of objects to pre-allocate
   */
  explicit ObjectPool(size_t initialSize = 64)
      : m_state(std::make_shared<SharedState>()) {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->pool.reserve(initialSize);
    for (size_t i = 0; i < initialSize; ++i) {
      m_state->pool.push_back(std::make_unique<T>());
    }
    m_totalAllocated.store(initialSize, std::memory_order_relaxed);
  }

  /**
   * @brief Construct pool with a factory function
   * @param initialSize Number of objects to pre-allocate
   * @param factory Function to create new objects
   */
  ObjectPool(size_t initialSize, std::function<std::unique_ptr<T>()> factory)
      : m_state(std::make_shared<SharedState>()),
        m_factory(std::move(factory)) {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->pool.reserve(initialSize);
    for (size_t i = 0; i < initialSize; ++i) {
      auto obj = m_factory ? m_factory() : std::make_unique<T>();
      if (!obj) {
        obj = std::make_unique<T>(); // Fallback if factory returns null
      }
      m_state->pool.push_back(std::move(obj));
    }
    m_totalAllocated.store(initialSize, std::memory_order_relaxed);
  }

  ~ObjectPool() { m_state->alive.store(false, std::memory_order_release); }

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
      std::lock_guard<std::mutex> lock(m_state->mutex);
      if (!m_state->pool.empty()) {
        raw = m_state->pool.back().release();
        m_state->pool.pop_back();
      }
    }

    if (!raw) {
      // Pool exhausted — allocate a new object
      if (m_factory) {
        auto obj = m_factory();
        raw = obj ? obj.release() : new T();
      } else {
        raw = new T();
      }
      m_totalAllocated.fetch_add(1, std::memory_order_relaxed);
    }

    m_acquireCount.fetch_add(1, std::memory_order_relaxed);

    // Capture shared_ptr to SharedState (not `this`) so the deleter can
    // safely access the mutex and pool even if the ObjectPool is destroyed.
    auto state = m_state;
    return std::shared_ptr<T>(raw, [state](T* obj) noexcept {
      if (state->alive.load(std::memory_order_acquire)) {
        try {
          std::lock_guard<std::mutex> lock(state->mutex);
          state->pool.push_back(std::unique_ptr<T>(obj));
          state->recycleCount.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
          // push_back failed (e.g., allocation) — delete directly
          delete obj;
        }
      } else {
        delete obj;
      }
    });
  }

  /**
   * @brief Get current number of available objects in the pool
   */
  size_t available() const {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->pool.size();
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
    return m_state->recycleCount.load(std::memory_order_relaxed);
  }

private:
  /**
   * @brief Shared state that outlives the ObjectPool if objects are still
   * in flight. The deleter captures a shared_ptr<SharedState> so it can
   * safely call recycle even after the ObjectPool destructor runs.
   */
  struct SharedState {
    std::mutex mutex;
    std::vector<std::unique_ptr<T>> pool;
    std::atomic<bool> alive{true};
    std::atomic<size_t> recycleCount{0};
  };

  std::shared_ptr<SharedState> m_state;
  std::function<std::unique_ptr<T>()> m_factory;

  std::atomic<size_t> m_totalAllocated{0};
  std::atomic<size_t> m_acquireCount{0};
};

} // namespace utils
} // namespace pinnacle
