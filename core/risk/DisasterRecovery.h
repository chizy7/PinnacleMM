#pragma once

#include "../utils/TimeUtils.h"

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pinnacle {
namespace risk {

struct BackupInfo {
  std::string label;
  std::string path;
  uint64_t timestamp{0};
  size_t sizeBytes{0};
  bool valid{false};
};

struct ReconciliationResult {
  bool positionsMatch{false};
  double localPosition{0.0};
  double exchangePosition{0.0};
  double discrepancy{0.0};
  std::string symbol;
  uint64_t timestamp{0};
};

class DisasterRecovery {
public:
  static DisasterRecovery& getInstance();

  void initialize(const std::string& backupDirectory);

  // Risk state persistence
  bool saveRiskState(const nlohmann::json& riskState,
                     const nlohmann::json& strategyState);
  nlohmann::json loadRiskState() const;
  nlohmann::json loadStrategyState() const;

  // Position reconciliation
  ReconciliationResult reconcilePosition(const std::string& symbol,
                                         double localPosition,
                                         double exchangePosition);

  // Backup management
  bool createBackup(const std::string& label);
  bool restoreBackup(const std::string& label);
  std::vector<BackupInfo> listBackups() const;
  bool deleteBackup(const std::string& label);

  // Integrity validation
  bool validateJournalIntegrity() const;
  bool validateSnapshotIntegrity() const;

  // Emergency state save (called on SIGTERM)
  bool emergencySave(const nlohmann::json& riskState,
                     const nlohmann::json& strategyState);

private:
  DisasterRecovery() = default;
  ~DisasterRecovery() = default;

  DisasterRecovery(const DisasterRecovery&) = delete;
  DisasterRecovery& operator=(const DisasterRecovery&) = delete;

  std::string m_backupDirectory;
  mutable std::mutex m_mutex;

  std::string getBackupPath(const std::string& label) const;
  std::string getRiskStatePath() const;
  std::string getStrategyStatePath() const;
};

} // namespace risk
} // namespace pinnacle
