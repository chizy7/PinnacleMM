#include "ResourceAllocator.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace instrument {

std::unordered_map<std::string, CoreAssignment>
ResourceAllocator::allocate(const std::vector<std::string>& symbols) const {
  std::unordered_map<std::string, CoreAssignment> assignments;

  int numCores = getAvailableCores();
  int numInstruments = static_cast<int>(symbols.size());

  if (numInstruments == 0) {
    return assignments;
  }

  if (numCores <= 0) {
    spdlog::error("Invalid core count {}; defaulting to 1", numCores);
    numCores = 1;
  }

  // Reserve core 0 for OS / main thread
  int availableCores = std::max(1, numCores - 1);

  // Each instrument ideally needs 2 cores (strategy + simulator)
  int coresPerInstrument = std::max(1, availableCores / numInstruments);

  int nextCore = 1; // Start from core 1

  for (int i = 0; i < numInstruments; ++i) {
    CoreAssignment assignment;
    assignment.symbol = symbols[i];

    // Assign strategy core (stay in range [1, numCores))
    assignment.strategyCore = 1 + ((nextCore - 1) % availableCores);
    nextCore++;

    // Assign simulator core if we have enough cores
    if (coresPerInstrument >= 2) {
      assignment.simulatorCore = 1 + ((nextCore - 1) % availableCores);
      nextCore++;
    } else {
      // Share core with strategy
      assignment.simulatorCore = assignment.strategyCore;
    }

    // Higher priority for instruments listed first
    assignment.priority = numInstruments - i;

    assignments[symbols[i]] = assignment;

    spdlog::info("[{}] Core assignment: strategy={} simulator={} priority={}",
                 symbols[i], assignment.strategyCore, assignment.simulatorCore,
                 assignment.priority);
  }

  return assignments;
}

int ResourceAllocator::getAvailableCores() const {
  return utils::ThreadAffinity::getNumCores();
}

bool ResourceAllocator::applyAssignment(const CoreAssignment& assignment,
                                        bool isStrategy) {
  int core = isStrategy ? assignment.strategyCore : assignment.simulatorCore;
  if (core < 0) {
    return false;
  }

  bool result = utils::ThreadAffinity::pinToCore(core);

  std::string threadType = isStrategy ? "strategy" : "simulator";
  std::string threadName = assignment.symbol + "_" + threadType;
  utils::ThreadAffinity::setThreadName(threadName);

  return result;
}

} // namespace instrument
} // namespace pinnacle
