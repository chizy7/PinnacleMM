#include "DisasterRecovery.h"
#include "../utils/AuditLogger.h"
#include "RiskManager.h"

#include <boost/filesystem.hpp>
#include <cmath>
#include <fstream>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace risk {

using pinnacle::utils::AuditLogger;
namespace bfs = boost::filesystem;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
DisasterRecovery& DisasterRecovery::getInstance() {
  static DisasterRecovery instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
void DisasterRecovery::initialize(const std::string& backupDirectory) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_backupDirectory = backupDirectory;

  try {
    if (!bfs::exists(m_backupDirectory)) {
      bfs::create_directories(m_backupDirectory);
      spdlog::info("DisasterRecovery: created backup directory: {}",
                   m_backupDirectory);
    }
  } catch (const bfs::filesystem_error& e) {
    spdlog::error("DisasterRecovery: failed to create backup directory {}: {}",
                  m_backupDirectory, e.what());
    return;
  }

  spdlog::info("DisasterRecovery initialized - backupDir={}",
               m_backupDirectory);
  AUDIT_SYSTEM_EVENT("DisasterRecovery initialized", true);
}

// ---------------------------------------------------------------------------
// Risk state persistence
// ---------------------------------------------------------------------------
bool DisasterRecovery::saveRiskState(const nlohmann::json& riskState,
                                     const nlohmann::json& strategyState) {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    // Atomic write for risk state: write to temp file, then rename
    std::string riskPath = getRiskStatePath();
    std::string riskTmpPath = riskPath + ".tmp";

    {
      std::ofstream ofs(riskTmpPath, std::ios::trunc);
      if (!ofs.is_open()) {
        spdlog::error("DisasterRecovery: failed to open tmp file for risk "
                      "state: {}",
                      riskTmpPath);
        return false;
      }
      ofs << riskState.dump(2);
      ofs.flush();
    }

    bfs::rename(riskTmpPath, riskPath);

    // Atomic write for strategy state
    std::string strategyPath = getStrategyStatePath();
    std::string strategyTmpPath = strategyPath + ".tmp";

    {
      std::ofstream ofs(strategyTmpPath, std::ios::trunc);
      if (!ofs.is_open()) {
        spdlog::error("DisasterRecovery: failed to open tmp file for strategy "
                      "state: {}",
                      strategyTmpPath);
        return false;
      }
      ofs << strategyState.dump(2);
      ofs.flush();
    }

    bfs::rename(strategyTmpPath, strategyPath);

    spdlog::debug("DisasterRecovery: saved risk and strategy state");
    return true;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to save risk state: {}", e.what());
    return false;
  }
}

nlohmann::json DisasterRecovery::loadRiskState() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string riskPath = getRiskStatePath();
  if (!bfs::exists(riskPath)) {
    spdlog::info("DisasterRecovery: no risk state file found at {}", riskPath);
    return nlohmann::json{};
  }

  try {
    std::ifstream ifs(riskPath);
    if (!ifs.is_open()) {
      spdlog::error("DisasterRecovery: failed to open risk state file: {}",
                    riskPath);
      return nlohmann::json{};
    }
    nlohmann::json j;
    ifs >> j;
    spdlog::info("DisasterRecovery: loaded risk state from {}", riskPath);
    return j;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to parse risk state from {}: {}",
                  riskPath, e.what());
    return nlohmann::json{};
  }
}

nlohmann::json DisasterRecovery::loadStrategyState() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::string strategyPath = getStrategyStatePath();
  if (!bfs::exists(strategyPath)) {
    spdlog::info("DisasterRecovery: no strategy state file found at {}",
                 strategyPath);
    return nlohmann::json{};
  }

  try {
    std::ifstream ifs(strategyPath);
    if (!ifs.is_open()) {
      spdlog::error("DisasterRecovery: failed to open strategy state file: {}",
                    strategyPath);
      return nlohmann::json{};
    }
    nlohmann::json j;
    ifs >> j;
    spdlog::info("DisasterRecovery: loaded strategy state from {}",
                 strategyPath);
    return j;
  } catch (const std::exception& e) {
    spdlog::error(
        "DisasterRecovery: failed to parse strategy state from {}: {}",
        strategyPath, e.what());
    return nlohmann::json{};
  }
}

// ---------------------------------------------------------------------------
// Position reconciliation
// ---------------------------------------------------------------------------
ReconciliationResult DisasterRecovery::reconcilePosition(
    const std::string& symbol, double localPosition, double exchangePosition) {
  ReconciliationResult result;
  result.symbol = symbol;
  result.localPosition = localPosition;
  result.exchangePosition = exchangePosition;
  result.discrepancy = localPosition - exchangePosition;
  result.timestamp = utils::TimeUtils::getCurrentNanos();

  // Use a small epsilon for floating-point comparison
  constexpr double epsilon = 1e-8;
  result.positionsMatch = std::abs(result.discrepancy) < epsilon;

  if (result.positionsMatch) {
    spdlog::info("DisasterRecovery: position reconciliation OK for {} - "
                 "local={} exchange={}",
                 symbol, localPosition, exchangePosition);
  } else {
    spdlog::warn("DisasterRecovery: position MISMATCH for {} - local={} "
                 "exchange={} discrepancy={}",
                 symbol, localPosition, exchangePosition, result.discrepancy);
    AUDIT_SYSTEM_EVENT("Position mismatch detected for " + symbol +
                           " discrepancy=" + std::to_string(result.discrepancy),
                       false);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Backup management
// ---------------------------------------------------------------------------
bool DisasterRecovery::createBackup(const std::string& label) {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    std::string backupPath = getBackupPath(label);

    if (bfs::exists(backupPath)) {
      spdlog::warn("DisasterRecovery: backup label '{}' already exists, "
                   "overwriting",
                   label);
      bfs::remove_all(backupPath);
    }

    bfs::create_directories(backupPath);

    // Copy current risk state file if it exists
    std::string riskPath = getRiskStatePath();
    if (bfs::exists(riskPath)) {
      bfs::copy_file(riskPath, backupPath + "/risk_state.json",
                     bfs::copy_options::overwrite_existing);
    }

    // Copy current strategy state file if it exists
    std::string strategyPath = getStrategyStatePath();
    if (bfs::exists(strategyPath)) {
      bfs::copy_file(strategyPath, backupPath + "/strategy_state.json",
                     bfs::copy_options::overwrite_existing);
    }

    // Copy journal files from the persistence data directory.
    // m_backupDirectory is typically "data/backups", so parent_path() gives
    // us "data" which is the PersistenceManager's data root.  Journals live
    // at "data/journals".
    bfs::path journalsDir =
        bfs::path(m_backupDirectory).parent_path() / "journals";
    if (bfs::exists(journalsDir) && bfs::is_directory(journalsDir)) {
      bfs::path destJournals = bfs::path(backupPath) / "journals";
      bfs::create_directories(destJournals);

      for (bfs::directory_iterator it(journalsDir);
           it != bfs::directory_iterator(); ++it) {
        if (bfs::is_regular_file(it->path())) {
          bfs::copy_file(it->path(), destJournals / it->path().filename(),
                         bfs::copy_options::overwrite_existing);
        }
      }
    }

    // Write a metadata file with the backup timestamp
    nlohmann::json meta;
    meta["label"] = label;
    meta["timestamp"] = utils::TimeUtils::getCurrentNanos();
    meta["iso_time"] = utils::TimeUtils::getCurrentISOTimestamp();

    {
      std::ofstream ofs(backupPath + "/backup_meta.json");
      if (ofs.is_open()) {
        ofs << meta.dump(2);
      }
    }

    spdlog::info("DisasterRecovery: backup '{}' created at {}", label,
                 backupPath);
    AUDIT_SYSTEM_EVENT("Backup created: " + label, true);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to create backup '{}': {}", label,
                  e.what());
    AUDIT_SYSTEM_EVENT("Backup creation failed: " + label, false);
    return false;
  }
}

bool DisasterRecovery::restoreBackup(const std::string& label) {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    std::string backupPath = getBackupPath(label);

    if (!bfs::exists(backupPath)) {
      spdlog::error("DisasterRecovery: backup '{}' not found at {}", label,
                    backupPath);
      return false;
    }

    // Restore risk state
    std::string backupRisk = backupPath + "/risk_state.json";
    if (bfs::exists(backupRisk)) {
      bfs::copy_file(backupRisk, getRiskStatePath(),
                     bfs::copy_options::overwrite_existing);
      spdlog::info("DisasterRecovery: restored risk state from backup '{}'",
                   label);
    }

    // Restore strategy state
    std::string backupStrategy = backupPath + "/strategy_state.json";
    if (bfs::exists(backupStrategy)) {
      bfs::copy_file(backupStrategy, getStrategyStatePath(),
                     bfs::copy_options::overwrite_existing);
      spdlog::info("DisasterRecovery: restored strategy state from backup '{}'",
                   label);
    }

    // Restore journal files
    bfs::path backupJournals = bfs::path(backupPath) / "journals";
    if (bfs::exists(backupJournals) && bfs::is_directory(backupJournals)) {
      bfs::path journalsDir =
          bfs::path(m_backupDirectory).parent_path() / "journals";
      bfs::create_directories(journalsDir);

      for (bfs::directory_iterator it(backupJournals);
           it != bfs::directory_iterator(); ++it) {
        if (bfs::is_regular_file(it->path())) {
          bfs::copy_file(it->path(), journalsDir / it->path().filename(),
                         bfs::copy_options::overwrite_existing);
        }
      }
      spdlog::info("DisasterRecovery: restored journal files from backup '{}'",
                   label);
    }

    spdlog::info("DisasterRecovery: backup '{}' restored successfully", label);
    AUDIT_SYSTEM_EVENT("Backup restored: " + label, true);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to restore backup '{}': {}", label,
                  e.what());
    AUDIT_SYSTEM_EVENT("Backup restore failed: " + label, false);
    return false;
  }
}

std::vector<BackupInfo> DisasterRecovery::listBackups() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<BackupInfo> backups;

  try {
    if (!bfs::exists(m_backupDirectory)) {
      return backups;
    }

    for (bfs::directory_iterator it(m_backupDirectory);
         it != bfs::directory_iterator(); ++it) {
      if (!bfs::is_directory(it->path())) {
        continue;
      }

      BackupInfo info;
      info.label = it->path().filename().string();
      info.path = it->path().string();

      // Try to read metadata
      std::string metaPath = it->path().string() + "/backup_meta.json";
      if (bfs::exists(metaPath)) {
        try {
          std::ifstream ifs(metaPath);
          nlohmann::json meta;
          ifs >> meta;
          info.timestamp = meta.value("timestamp", uint64_t{0});
          info.valid = true;
        } catch (const std::exception& e) {
          spdlog::warn("DisasterRecovery: failed to read metadata for "
                       "backup '{}': {}",
                       info.label, e.what());
          info.valid = false;
        }
      }

      // Calculate total size of the backup directory
      size_t totalSize = 0;
      for (bfs::recursive_directory_iterator rit(it->path());
           rit != bfs::recursive_directory_iterator(); ++rit) {
        if (bfs::is_regular_file(rit->path())) {
          totalSize += static_cast<size_t>(bfs::file_size(rit->path()));
        }
      }
      info.sizeBytes = totalSize;

      backups.push_back(std::move(info));
    }
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to list backups: {}", e.what());
  }

  return backups;
}

bool DisasterRecovery::deleteBackup(const std::string& label) {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    std::string backupPath = getBackupPath(label);

    if (!bfs::exists(backupPath)) {
      spdlog::warn("DisasterRecovery: backup '{}' does not exist", label);
      return false;
    }

    bfs::remove_all(backupPath);

    spdlog::info("DisasterRecovery: backup '{}' deleted", label);
    AUDIT_SYSTEM_EVENT("Backup deleted: " + label, true);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: failed to delete backup '{}': {}", label,
                  e.what());
    return false;
  }
}

// ---------------------------------------------------------------------------
// Integrity validation
// ---------------------------------------------------------------------------
bool DisasterRecovery::validateJournalIntegrity() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    bfs::path journalsDir =
        bfs::path(m_backupDirectory).parent_path() / "journals";

    if (!bfs::exists(journalsDir)) {
      spdlog::warn("DisasterRecovery: journals directory does not exist: {}",
                   journalsDir.string());
      return false;
    }

    bool allValid = true;
    int fileCount = 0;

    for (bfs::directory_iterator it(journalsDir);
         it != bfs::directory_iterator(); ++it) {
      if (!bfs::is_regular_file(it->path())) {
        continue;
      }

      std::string filename = it->path().filename().string();
      // Only check .journal files
      if (filename.size() < 8 ||
          filename.substr(filename.size() - 8) != ".journal") {
        continue;
      }

      fileCount++;

      // Check that the file is non-empty
      auto fileSize = bfs::file_size(it->path());
      if (fileSize == 0) {
        spdlog::error("DisasterRecovery: journal file is empty: {}",
                      it->path().string());
        allValid = false;
      } else {
        spdlog::debug("DisasterRecovery: journal file OK: {} ({} bytes)",
                      it->path().string(), fileSize);
      }
    }

    if (fileCount == 0) {
      spdlog::warn("DisasterRecovery: no journal files found in {}",
                   journalsDir.string());
      return false;
    }

    spdlog::info("DisasterRecovery: journal integrity check complete - {} "
                 "files, allValid={}",
                 fileCount, allValid);
    return allValid;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: journal integrity check failed: {}",
                  e.what());
    return false;
  }
}

bool DisasterRecovery::validateSnapshotIntegrity() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  try {
    bfs::path snapshotsDir =
        bfs::path(m_backupDirectory).parent_path() / "snapshots";

    if (!bfs::exists(snapshotsDir)) {
      spdlog::warn("DisasterRecovery: snapshots directory does not exist: {}",
                   snapshotsDir.string());
      return false;
    }

    bool allValid = true;
    int dirCount = 0;

    for (bfs::directory_iterator it(snapshotsDir);
         it != bfs::directory_iterator(); ++it) {
      if (!bfs::is_directory(it->path())) {
        continue;
      }

      dirCount++;

      // Check that the snapshot directory contains at least one file
      bool hasFiles = false;
      for (bfs::directory_iterator sit(it->path());
           sit != bfs::directory_iterator(); ++sit) {
        if (bfs::is_regular_file(sit->path())) {
          hasFiles = true;
          break;
        }
      }

      if (!hasFiles) {
        spdlog::error("DisasterRecovery: snapshot directory is empty: {}",
                      it->path().string());
        allValid = false;
      } else {
        spdlog::debug("DisasterRecovery: snapshot directory OK: {}",
                      it->path().string());
      }
    }

    if (dirCount == 0) {
      spdlog::warn("DisasterRecovery: no snapshot directories found in {}",
                   snapshotsDir.string());
      return false;
    }

    spdlog::info("DisasterRecovery: snapshot integrity check complete - {} "
                 "directories, allValid={}",
                 dirCount, allValid);
    return allValid;
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: snapshot integrity check failed: {}",
                  e.what());
    return false;
  }
}

// ---------------------------------------------------------------------------
// Emergency save (synchronous, called on SIGTERM)
// ---------------------------------------------------------------------------
bool DisasterRecovery::emergencySave(const nlohmann::json& riskState,
                                     const nlohmann::json& strategyState) {
  // No backup creation -- just write the state files as fast as possible.
  // We still acquire the mutex to avoid partial writes from a concurrent
  // saveRiskState() call, but we do NOT create a labeled backup.
  std::lock_guard<std::mutex> lock(m_mutex);

  spdlog::warn("DisasterRecovery: EMERGENCY SAVE triggered");

  bool success = true;

  try {
    // Write risk state directly (no atomic rename to minimize latency)
    std::string riskPath = getRiskStatePath();
    {
      std::ofstream ofs(riskPath, std::ios::trunc);
      if (!ofs.is_open()) {
        spdlog::error(
            "DisasterRecovery: emergency save failed to open risk state: {}",
            riskPath);
        success = false;
      } else {
        ofs << riskState.dump(2);
        ofs.flush();
      }
    }

    // Write strategy state directly
    std::string strategyPath = getStrategyStatePath();
    {
      std::ofstream ofs(strategyPath, std::ios::trunc);
      if (!ofs.is_open()) {
        spdlog::error("DisasterRecovery: emergency save failed to open "
                      "strategy state: {}",
                      strategyPath);
        success = false;
      } else {
        ofs << strategyState.dump(2);
        ofs.flush();
      }
    }

    if (success) {
      spdlog::info("DisasterRecovery: emergency save completed successfully");
    } else {
      spdlog::error("DisasterRecovery: emergency save completed with errors");
    }
  } catch (const std::exception& e) {
    spdlog::error("DisasterRecovery: emergency save failed: {}", e.what());
    success = false;
  }

  AUDIT_SYSTEM_EVENT("Emergency state save", success);
  return success;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
std::string DisasterRecovery::getBackupPath(const std::string& label) const {
  return (bfs::path(m_backupDirectory) / label).string();
}

std::string DisasterRecovery::getRiskStatePath() const {
  return (bfs::path(m_backupDirectory) / "risk_state.json").string();
}

std::string DisasterRecovery::getStrategyStatePath() const {
  return (bfs::path(m_backupDirectory) / "strategy_state.json").string();
}

} // namespace risk
} // namespace pinnacle
