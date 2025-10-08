#pragma once

#include "../../core/utils/TimeUtils.h"
#include "../config/StrategyConfig.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace rl {

/**
 * @enum ActionType
 * @brief Types of actions the RL agent can take
 */
enum class ActionType : uint8_t {
  INCREASE_SPREAD = 0,
  DECREASE_SPREAD = 1,
  INCREASE_ORDER_SIZE = 2,
  DECREASE_ORDER_SIZE = 3,
  INCREASE_POSITION_LIMIT = 4,
  DECREASE_POSITION_LIMIT = 5,
  INCREASE_RISK_TOLERANCE = 6,
  DECREASE_RISK_TOLERANCE = 7,
  MAINTAIN_CURRENT = 8
};

/**
 * @enum ParameterType
 * @brief Types of parameters that can be adapted
 */
enum class ParameterType : uint8_t {
  BASE_SPREAD_BPS = 0,
  ORDER_QUANTITY = 1,
  MAX_POSITION = 2,
  INVENTORY_SKEW_FACTOR = 3,
  ML_CONFIDENCE_THRESHOLD = 4,
  FLOW_ADJUSTMENT_WEIGHT = 5,
  IMPACT_ADJUSTMENT_WEIGHT = 6
};

/**
 * @struct MarketState
 * @brief Represents the current market state for RL decision making
 */
struct MarketState {
  double volatility{0.0};      // Current market volatility
  double spread{0.0};          // Current bid-ask spread
  double volume{0.0};          // Recent trading volume
  double imbalance{0.0};       // Order book imbalance
  double momentum{0.0};        // Price momentum
  double liquidity{0.0};       // Market liquidity
  double timeOfDay{0.0};       // Normalized time of day [0-1]
  double dayOfWeek{0.0};       // Normalized day of week [0-1]
  double currentPosition{0.0}; // Current trading position
  double unrealizedPnL{0.0};   // Unrealized P&L

  // Discretize state for Q-learning
  size_t getStateHash() const;
  std::string toString() const;
};

/**
 * @struct ActionReward
 * @brief Represents an action taken and its resulting reward
 */
struct ActionReward {
  MarketState stateBefore;
  ActionType action;
  ParameterType parameter;
  double parameterValue; // Value after action
  MarketState stateAfter;
  double reward;     // Reward received
  double confidence; // Confidence in the reward
  uint64_t timestamp;
  uint64_t episodeId; // Episode identifier

  ActionReward() = default;
  ActionReward(const MarketState& before, ActionType act, ParameterType param,
               double value, const MarketState& after, double rew, uint64_t ts)
      : stateBefore(before), action(act), parameter(param),
        parameterValue(value), stateAfter(after), reward(rew), confidence(1.0),
        timestamp(ts), episodeId(0) {}
};

/**
 * @struct ParameterConstraints
 * @brief Defines valid ranges for parameter adjustments
 */
struct ParameterConstraints {
  double minValue;
  double maxValue;
  double stepSize;
  bool isDiscrete;

  ParameterConstraints(double min = 0.0, double max = 1.0, double step = 0.1,
                       bool discrete = false)
      : minValue(min), maxValue(max), stepSize(step), isDiscrete(discrete) {}

  double clamp(double value) const;
  double getNextValue(double current, bool increase) const;
};

/**
 * @struct QTableEntry
 * @brief Q-learning table entry
 */
struct QTableEntry {
  double qValue{0.0};
  uint32_t visitCount{0};
  double confidence{0.0};
  uint64_t lastUpdate{0};

  void update(double reward, double learningRate, double maxFutureQ);
  double getConfidenceWeightedValue() const;
};

/**
 * @struct EpsilonGreedyConfig
 * @brief Configuration for epsilon-greedy exploration
 */
struct EpsilonGreedyConfig {
  double initialEpsilon{0.3}; // Initial exploration rate
  double minEpsilon{0.05};    // Minimum exploration rate
  double epsilonDecay{0.995}; // Decay rate per episode
  uint32_t decaySteps{1000};  // Steps before decay

  double getCurrentEpsilon(uint32_t episode) const;
};

/**
 * @struct MultiArmedBanditConfig
 * @brief Configuration for multi-armed bandit strategy selection
 */
struct MultiArmedBanditConfig {
  double ucbConstant{1.4};     // UCB exploration constant
  double decayFactor{0.99};    // Reward decay factor
  uint32_t warmupPeriod{100};  // Episodes before using UCB
  bool enableContextual{true}; // Use contextual bandits

  double calculateUCB(double avgReward, uint32_t visits,
                      uint32_t totalVisits) const;
};

/**
 * @class QLearningAgent
 * @brief Q-learning implementation for parameter optimization
 */
class QLearningAgent {
public:
  struct Config {
    double learningRate{0.1};    // Learning rate for Q-updates
    double discountFactor{0.95}; // Future reward discount
    EpsilonGreedyConfig epsilonConfig;
    uint32_t maxQTableSize{100000};      // Maximum Q-table entries
    uint32_t stateDiscretization{10};    // State space discretization
    bool enableDoubleQLearning{true};    // Use Double Q-Learning
    bool enablePrioritizedReplay{false}; // Prioritized experience replay
  };

  explicit QLearningAgent(const Config& config);
  ~QLearningAgent() = default;

  // Core Q-learning interface
  ActionType selectAction(const MarketState& state, ParameterType parameter,
                          const std::vector<ActionType>& validActions);
  void updateQValue(const MarketState& stateBefore, ActionType action,
                    ParameterType parameter, double reward,
                    const MarketState& stateAfter);

  // Q-table management
  double getQValue(const MarketState& state, ActionType action,
                   ParameterType parameter) const;
  void setQValue(const MarketState& state, ActionType action,
                 ParameterType parameter, double value);

  // Statistics and monitoring
  size_t getQTableSize() const { return m_qTable.size(); }
  double getAverageQValue() const;
  uint32_t getCurrentEpisode() const { return m_currentEpisode; }
  std::string getStatistics() const;

  // Persistence
  bool saveQTable(const std::string& filename) const;
  bool loadQTable(const std::string& filename);
  void reset();

private:
  Config m_config;
  std::unordered_map<std::string, QTableEntry> m_qTable;
  std::unordered_map<std::string, QTableEntry>
      m_qTable2; // For Double Q-Learning
  mutable std::mutex m_qTableMutex;

  std::mt19937 m_rng;
  std::atomic<uint32_t> m_currentEpisode{0};
  std::atomic<uint64_t> m_totalActions{0};

  // Helper methods
  std::string getQKey(const MarketState& state, ActionType action,
                      ParameterType parameter) const;
  ActionType selectEpsilonGreedy(const MarketState& state,
                                 ParameterType parameter,
                                 const std::vector<ActionType>& validActions);
  ActionType
  selectBestAction(const MarketState& state, ParameterType parameter,
                   const std::vector<ActionType>& validActions) const;
  double getMaxQValue(const MarketState& state, ParameterType parameter) const;
};

/**
 * @class MultiArmedBandit
 * @brief Multi-armed bandit for strategy selection
 */
class MultiArmedBandit {
public:
  struct Strategy {
    std::string name;
    double averageReward{0.0};
    uint32_t visitCount{0};
    double rewardSum{0.0};
    double confidence{0.0};
    bool isActive{true};

    void updateReward(double reward, double decayFactor);
    double getUCBValue(uint32_t totalVisits, double ucbConstant) const;
  };

  explicit MultiArmedBandit(const MultiArmedBanditConfig& config);
  ~MultiArmedBandit() = default;

  // Bandit interface
  std::string selectStrategy(const MarketState& context = MarketState{});
  void updateReward(const std::string& strategy, double reward);

  // Strategy management
  void addStrategy(const std::string& name);
  void removeStrategy(const std::string& name);
  void setStrategyActive(const std::string& name, bool active);

  // Statistics
  std::vector<Strategy> getStrategies() const;
  Strategy getBestStrategy() const;
  std::string getStatistics() const;
  void reset();

private:
  MultiArmedBanditConfig m_config;
  std::unordered_map<std::string, Strategy> m_strategies;
  mutable std::mutex m_strategiesMutex;

  std::mt19937 m_rng;
  std::atomic<uint32_t> m_totalSelections{0};

  // Contextual bandit features (future extension)
  double calculateContextualReward(const std::string& strategy,
                                   const MarketState& context) const;
};

/**
 * @class RLParameterAdapter
 * @brief Main class for reinforcement learning-based parameter adaptation
 */
class RLParameterAdapter {
public:
  struct Config {
    // Q-learning configuration
    QLearningAgent::Config qLearningConfig;

    // Multi-armed bandit configuration
    MultiArmedBanditConfig banditConfig;

    // Parameter adaptation settings
    std::unordered_map<ParameterType, ParameterConstraints>
        parameterConstraints;
    uint64_t adaptationIntervalMs{5000}; // 5 seconds
    uint64_t episodeIntervalMs{60000};   // 1 minute episodes
    uint32_t minActionsPerEpisode{10};   // Minimum actions before episode end

    // Reward calculation settings
    double pnlRewardWeight{0.4};       // Weight for P&L in reward
    double fillRateRewardWeight{0.3};  // Weight for fill rate
    double riskRewardWeight{0.2};      // Weight for risk metrics
    double stabilityRewardWeight{0.1}; // Weight for parameter stability

    // Performance monitoring
    bool enablePerformanceTracking{true};
    uint64_t performanceReportIntervalMs{300000}; // 5 minutes
    uint32_t maxHistorySize{10000};

    // Safety and risk management
    double maxParameterChangePerEpisode{0.1}; // 10% max change per episode
    bool enableSafetyConstraints{true};
    double minRewardForUpdate{-0.5}; // Minimum reward to trigger update

    Config();
  };

  explicit RLParameterAdapter(const std::string& symbol,
                              const Config& config = Config{});
  ~RLParameterAdapter();

  // Delete copy and move operations
  RLParameterAdapter(const RLParameterAdapter&) = delete;
  RLParameterAdapter& operator=(const RLParameterAdapter&) = delete;
  RLParameterAdapter(RLParameterAdapter&&) = delete;
  RLParameterAdapter& operator=(RLParameterAdapter&&) = delete;

  // Lifecycle management
  bool initialize();
  bool start();
  bool stop();
  bool isRunning() const { return m_isRunning.load(); }

  // Core RL interface
  void updateMarketState(const MarketState& state);
  void recordPerformance(double pnl, double fillRate, double riskMetric,
                         uint64_t timestamp);
  void adaptParameters(strategy::StrategyConfig& config);

  // Strategy selection
  std::string
  selectStrategy(const std::vector<std::string>& availableStrategies);
  void recordStrategyPerformance(const std::string& strategy, double reward);

  // Manual parameter control
  void setParameterValue(ParameterType parameter, double value);
  double getParameterValue(ParameterType parameter) const;
  bool isParameterAdaptationEnabled(ParameterType parameter) const;
  void enableParameterAdaptation(ParameterType parameter, bool enable);

  // Analytics and monitoring
  std::string getPerformanceStatistics() const;
  std::string getQLearningStatistics() const;
  std::string getBanditStatistics() const;

  // Episode management
  void forceEpisodeEnd();
  uint32_t getCurrentEpisode() const;
  uint64_t getActionCount() const;

  // Model persistence
  bool saveModel(const std::string& filename) const;
  bool loadModel(const std::string& filename);
  void reset();

  // Configuration updates
  bool updateConfig(const Config& config);
  Config getCurrentConfig() const { return m_config; }

private:
  std::string m_symbol;
  Config m_config;

  // RL components
  std::unique_ptr<QLearningAgent> m_qAgent;
  std::unique_ptr<MultiArmedBandit> m_bandit;

  // State management
  MarketState m_currentState;
  MarketState m_previousState;
  mutable std::mutex m_stateMutex;

  // Performance tracking
  struct PerformanceMetrics {
    double totalPnL{0.0};
    double totalFillRate{0.0};
    double totalRiskMetric{0.0};
    uint32_t sampleCount{0};
    uint64_t lastUpdate{0};

    void update(double pnl, double fillRate, double risk, uint64_t timestamp);
    double calculateReward(const Config& config) const;
    void reset();
  };

  PerformanceMetrics m_currentEpisodeMetrics;
  std::deque<PerformanceMetrics> m_episodeHistory;
  mutable std::mutex m_performanceMutex;

  // Parameter tracking
  std::unordered_map<ParameterType, double> m_currentParameters;
  std::unordered_map<ParameterType, bool> m_parameterAdaptationEnabled;
  mutable std::mutex m_parameterMutex;

  // Episode management
  std::atomic<bool> m_isRunning{false};
  std::atomic<uint32_t> m_currentEpisode{0};
  std::atomic<uint64_t> m_actionCount{0};
  uint64_t m_episodeStartTime{0};
  uint64_t m_lastAdaptationTime{0};

  // Action tracking
  std::deque<ActionReward> m_actionHistory;
  mutable std::mutex m_actionMutex;

  // Helper methods
  void processEpisodeEnd();
  double calculateReward(const PerformanceMetrics& metrics) const;
  void updateParameterFromAction(ActionType action, ParameterType parameter,
                                 strategy::StrategyConfig& config);
  std::vector<ActionType> getValidActions(ParameterType parameter) const;
  MarketState extractMarketState() const;
  void cleanupOldData();
  bool isEpisodeComplete() const;
  void applyParameterConstraints();

  // Reward calculation helpers
  double calculatePnLReward(double pnl) const;
  double calculateFillRateReward(double fillRate) const;
  double calculateRiskReward(double riskMetric) const;
  double calculateStabilityReward() const;
};

/**
 * @brief Helper function to convert ParameterType to string
 */
std::string parameterTypeToString(ParameterType type);

/**
 * @brief Helper function to convert ActionType to string
 */
std::string actionTypeToString(ActionType type);

/**
 * @brief Helper function to create default parameter constraints
 */
std::unordered_map<ParameterType, ParameterConstraints>
createDefaultParameterConstraints();

} // namespace rl
} // namespace pinnacle
