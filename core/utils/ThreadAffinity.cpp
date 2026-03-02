#include "ThreadAffinity.h"

#include <spdlog/spdlog.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace pinnacle {
namespace utils {

bool ThreadAffinity::pinToCore(int coreId) {
  if (coreId < 0 || coreId >= getNumCores()) {
    spdlog::warn("Invalid core ID {} (available: 0-{})", coreId,
                 getNumCores() - 1);
    return false;
  }

#ifdef __APPLE__
  // macOS uses thread affinity tags (hints, not hard pinning)
  thread_affinity_policy_data_t policy;
  policy.affinity_tag = coreId + 1; // 0 means no affinity
  mach_port_t thread_port = mach_thread_self();
  kern_return_t ret = thread_policy_set(
      thread_port, THREAD_AFFINITY_POLICY,
      reinterpret_cast<thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
  mach_port_deallocate(mach_task_self(), thread_port);
  if (ret != KERN_SUCCESS) {
    spdlog::warn("Failed to set thread affinity to core {}", coreId);
    return false;
  }
  return true;

#elif defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(coreId, &cpuset);

  int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (ret != 0) {
    spdlog::warn("Failed to set thread affinity to core {}: {}", coreId,
                 strerror(ret));
    return false;
  }
  return true;

#else
  spdlog::warn("Thread affinity not supported on this platform");
  return false;
#endif
}

void ThreadAffinity::setThreadName(const std::string& name) {
#ifdef __APPLE__
  // macOS: only the calling thread can set its own name
  pthread_setname_np(name.substr(0, 63).c_str());
#elif defined(__linux__)
  // Linux: name limited to 16 chars including null terminator
  pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
}

int ThreadAffinity::getNumCores() {
  int cores = static_cast<int>(std::thread::hardware_concurrency());
  return cores > 0 ? cores : 1;
}

bool ThreadAffinity::pinThreadToCore(std::thread& thread, int coreId) {
  if (!thread.joinable()) {
    return false;
  }

  if (coreId < 0 || coreId >= getNumCores()) {
    spdlog::warn("Invalid core ID {} (available: 0-{})", coreId,
                 getNumCores() - 1);
    return false;
  }

#ifdef __APPLE__
  // macOS doesn't support pinning another thread from outside easily.
  // The thread itself should call pinToCore().
  spdlog::debug("macOS: thread should call pinToCore() itself");
  return false;

#elif defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(coreId, &cpuset);

  int ret = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                   &cpuset);
  if (ret != 0) {
    spdlog::warn("Failed to pin thread to core {}: {}", coreId, strerror(ret));
    return false;
  }
  return true;

#else
  return false;
#endif
}

} // namespace utils
} // namespace pinnacle
