#include "RLParameterAdapter.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>

namespace pinnacle {
namespace rl {

using namespace utils;

// MarketState implementation
size_t MarketState::getStateHash() const {
  // Discretize state for Q-learning
  auto hashValue = std::hash<double>{}(volatility * 1000) ^
                   std::hash<double>{}(spread * 1000000) ^
                   std::hash<double>{}(volume * 100) ^
                   std::hash<double>{}(imbalance * 1000) ^
                   std::hash<double>{}(momentum * 1000) ^
                   std::hash<double>{}(liquidity * 100) ^
                   std::hash<double>{}(timeOfDay * 24) ^
                   std::hash<double>{}(dayOfWeek * 7) ^
                   std::hash<double>{}(currentPosition * 100) ^
                   std::hash<double>{}(unrealizedPnL * 100);
  return hashValue;
}

std::string MarketState::toString() const {
  std::ostringstream oss;
  oss << "MarketState{vol=" << volatility << ", spread=" << spread
      << ", volume=" << volume << ", imbalance=" << imbalance
      << ", momentum=" << momentum << ", liquidity=" << liquidity
      << ", time=" << timeOfDay << ", day=" << dayOfWeek
      << ", position=" << currentPosition << ", pnl=" << unrealizedPnL << "}";
  return oss.str();
}

// ParameterConstraints implementation
double ParameterConstraints::clamp(double value) const {
  return std::max(minValue, std::min(maxValue, value));
}

double ParameterConstraints::getNextValue(double current, bool increase) const {
  double nextValue = increase ? current + stepSize : current - stepSize;
  return clamp(nextValue);
}

// QTableEntry implementation
void QTableEntry::update(double reward, double learningRate,
                         double maxFutureQ) {
  double error = reward + maxFutureQ - qValue;
  qValue += learningRate * error;
  visitCount++;
  confidence = std::min(1.0, visitCount / 100.0);
  lastUpdate = TimeUtils::getCurrentNanos();
}

double QTableEntry::getConfidenceWeightedValue() const {
  return qValue * confidence;
}

// EpsilonGreedyConfig implementation
double EpsilonGreedyConfig::getCurrentEpsilon(uint32_t episode) const {
  if (episode < decaySteps) {
    double decay = std::pow(epsilonDecay, episode);
    return std::max(minEpsilon, initialEpsilon * decay);
  }
  return minEpsilon;
}

// MultiArmedBanditConfig implementation
double MultiArmedBanditConfig::calculateUCB(double avgReward, uint32_t visits,
                                            uint32_t totalVisits) const {
  if (visits == 0)
    return std::numeric_limits<double>::max();

  double exploration = ucbConstant * std::sqrt(std::log(totalVisits) / visits);
  return avgReward + exploration;
}

// QLearningAgent implementation
QLearningAgent::QLearningAgent(const Config& config)
    : m_config(config), m_rng(std::random_device{}()) {
  m_qTable.reserve(m_config.maxQTableSize);
  if (m_config.enableDoubleQLearning) {
    m_qTable2.reserve(m_config.maxQTableSize);
  }
}

ActionType
QLearningAgent::selectAction(const MarketState& state, ParameterType parameter,
                             const std::vector<ActionType>& validActions) {
  if (validActions.empty()) {
    return ActionType::MAINTAIN_CURRENT;
  }

  m_totalActions++;

  double epsilon = m_config.epsilonConfig.getCurrentEpsilon(m_currentEpisode);
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  if (dist(m_rng) < epsilon) {
    return selectEpsilonGreedy(state, parameter, validActions);
  } else {
    return selectBestAction(state, parameter, validActions);
  }
}

void QLearningAgent::updateQValue(const MarketState& stateBefore,
                                  ActionType action, ParameterType parameter,
                                  double reward,
                                  const MarketState& stateAfter) {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::string key = getQKey(stateBefore, action, parameter);
  double maxFutureQ = getMaxQValue(stateAfter, parameter);

  if (m_config.enableDoubleQLearning) {
    // Double Q-Learning: Randomly choose which table to update
    std::uniform_int_distribution<int> tableDist(0, 1);
    bool useTable1 = tableDist(m_rng) == 0;

    auto& table = useTable1 ? m_qTable : m_qTable2;
    auto& otherTable = useTable1 ? m_qTable2 : m_qTable;

    // Get max action from current table
    ActionType maxAction = ActionType::MAINTAIN_CURRENT;
    double maxQ = std::numeric_limits<double>::lowest();

    for (const auto& validAction :
         {ActionType::INCREASE_SPREAD, ActionType::DECREASE_SPREAD,
          ActionType::INCREASE_ORDER_SIZE, ActionType::DECREASE_ORDER_SIZE,
          ActionType::MAINTAIN_CURRENT}) {
      std::string futureKey = getQKey(stateAfter, validAction, parameter);
      if (table.find(futureKey) != table.end() &&
          table[futureKey].qValue > maxQ) {
        maxQ = table[futureKey].qValue;
        maxAction = validAction;
      }
    }

    // Use other table's Q-value for the max action
    std::string maxKey = getQKey(stateAfter, maxAction, parameter);
    double futureQValue = 0.0;
    if (otherTable.find(maxKey) != otherTable.end()) {
      futureQValue = otherTable[maxKey].qValue;
    }

    table[key].update(reward, m_config.learningRate,
                      m_config.discountFactor * futureQValue);
  } else {
    // Standard Q-Learning
    m_qTable[key].update(reward, m_config.learningRate,
                         m_config.discountFactor * maxFutureQ);
  }
}

double QLearningAgent::getQValue(const MarketState& state, ActionType action,
                                 ParameterType parameter) const {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::string key = getQKey(state, action, parameter);
  auto it = m_qTable.find(key);
  if (it != m_qTable.end()) {
    return it->second.getConfidenceWeightedValue();
  }
  return 0.0;
}

void QLearningAgent::setQValue(const MarketState& state, ActionType action,
                               ParameterType parameter, double value) {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::string key = getQKey(state, action, parameter);
  m_qTable[key].qValue = value;
  m_qTable[key].visitCount = 1;
  m_qTable[key].confidence = 1.0;
  m_qTable[key].lastUpdate = TimeUtils::getCurrentNanos();
}

double QLearningAgent::getAverageQValue() const {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  if (m_qTable.empty())
    return 0.0;

  double sum = 0.0;
  for (const auto& [key, entry] : m_qTable) {
    sum += entry.qValue;
  }
  return sum / m_qTable.size();
}

std::string QLearningAgent::getStatistics() const {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::ostringstream oss;
  oss << "Q-Learning Agent Statistics:\n";
  oss << "  Q-Table Size: " << m_qTable.size() << "\n";
  oss << "  Average Q-Value: " << getAverageQValue() << "\n";
  oss << "  Current Episode: " << m_currentEpisode << "\n";
  oss << "  Total Actions: " << m_totalActions << "\n";
  oss << "  Current Epsilon: "
      << m_config.epsilonConfig.getCurrentEpsilon(m_currentEpisode) << "\n";
  oss << "  Double Q-Learning: "
      << (m_config.enableDoubleQLearning ? "Enabled" : "Disabled") << "\n";

  return oss.str();
}

bool QLearningAgent::saveQTable(const std::string& filename) const {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open())
    return false;

  // Save Q-table size
  size_t tableSize = m_qTable.size();
  file.write(reinterpret_cast<const char*>(&tableSize), sizeof(tableSize));

  // Save Q-table entries
  for (const auto& [key, entry] : m_qTable) {
    size_t keySize = key.size();
    file.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
    file.write(key.c_str(), keySize);
    file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

  // Save metadata
  file.write(reinterpret_cast<const char*>(&m_currentEpisode),
             sizeof(m_currentEpisode));
  file.write(reinterpret_cast<const char*>(&m_totalActions),
             sizeof(m_totalActions));

  return file.good();
}

bool QLearningAgent::loadQTable(const std::string& filename) {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open())
    return false;

  m_qTable.clear();

  // Load Q-table size
  size_t tableSize;
  file.read(reinterpret_cast<char*>(&tableSize), sizeof(tableSize));

  // Load Q-table entries
  for (size_t i = 0; i < tableSize; ++i) {
    size_t keySize;
    file.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));

    std::string key(keySize, '\0');
    file.read(&key[0], keySize);

    QTableEntry entry;
    file.read(reinterpret_cast<char*>(&entry), sizeof(entry));

    m_qTable[key] = entry;
  }

  // Load metadata
  uint32_t episode;
  uint64_t actions;
  file.read(reinterpret_cast<char*>(&episode), sizeof(episode));
  file.read(reinterpret_cast<char*>(&actions), sizeof(actions));

  m_currentEpisode = episode;
  m_totalActions = actions;

  return file.good();
}

void QLearningAgent::reset() {
  std::lock_guard<std::mutex> lock(m_qTableMutex);

  m_qTable.clear();
  m_qTable2.clear();
  m_currentEpisode = 0;
  m_totalActions = 0;
}

std::string QLearningAgent::getQKey(const MarketState& state, ActionType action,
                                    ParameterType parameter) const {
  // Create discretized state key for Q-table lookup
  int volBucket =
      static_cast<int>(state.volatility * m_config.stateDiscretization);
  int spreadBucket =
      static_cast<int>(state.spread * m_config.stateDiscretization * 1000);
  int volumeBucket =
      static_cast<int>(state.volume * m_config.stateDiscretization);
  int imbalanceBucket = static_cast<int>((state.imbalance + 1.0) *
                                         m_config.stateDiscretization / 2.0);
  int momentumBucket = static_cast<int>((state.momentum + 1.0) *
                                        m_config.stateDiscretization / 2.0);
  int liquidityBucket =
      static_cast<int>(state.liquidity * m_config.stateDiscretization);
  int timeBucket =
      static_cast<int>(state.timeOfDay * m_config.stateDiscretization);
  int dayBucket =
      static_cast<int>(state.dayOfWeek * m_config.stateDiscretization);
  int positionBucket = static_cast<int>((state.currentPosition + 1.0) *
                                        m_config.stateDiscretization / 2.0);

  std::ostringstream oss;
  oss << volBucket << "_" << spreadBucket << "_" << volumeBucket << "_"
      << imbalanceBucket << "_" << momentumBucket << "_" << liquidityBucket
      << "_" << timeBucket << "_" << dayBucket << "_" << positionBucket << "_"
      << static_cast<int>(action) << "_" << static_cast<int>(parameter);

  return oss.str();
}

ActionType QLearningAgent::selectEpsilonGreedy(
    const MarketState& /* state */, ParameterType /* parameter */,
    const std::vector<ActionType>& validActions) {
  std::uniform_int_distribution<size_t> dist(0, validActions.size() - 1);
  return validActions[dist(m_rng)];
}

ActionType QLearningAgent::selectBestAction(
    const MarketState& state, ParameterType parameter,
    const std::vector<ActionType>& validActions) const {
  ActionType bestAction = validActions[0];
  double bestQValue = getQValue(state, bestAction, parameter);

  for (const auto& action : validActions) {
    double qValue = getQValue(state, action, parameter);
    if (qValue > bestQValue) {
      bestQValue = qValue;
      bestAction = action;
    }
  }

  return bestAction;
}

double QLearningAgent::getMaxQValue(const MarketState& state,
                                    ParameterType parameter) const {
  std::vector<ActionType> allActions = {
      ActionType::INCREASE_SPREAD,         ActionType::DECREASE_SPREAD,
      ActionType::INCREASE_ORDER_SIZE,     ActionType::DECREASE_ORDER_SIZE,
      ActionType::INCREASE_POSITION_LIMIT, ActionType::DECREASE_POSITION_LIMIT,
      ActionType::INCREASE_RISK_TOLERANCE, ActionType::DECREASE_RISK_TOLERANCE,
      ActionType::MAINTAIN_CURRENT};

  double maxQ = std::numeric_limits<double>::lowest();
  for (const auto& action : allActions) {
    double qValue = getQValue(state, action, parameter);
    maxQ = std::max(maxQ, qValue);
  }

  return maxQ;
}

// MultiArmedBandit Strategy implementation
void MultiArmedBandit::Strategy::updateReward(double reward,
                                              double decayFactor) {
  rewardSum = rewardSum * decayFactor + reward;
  visitCount++;
  averageReward = rewardSum / visitCount;
  confidence = std::min(1.0, visitCount / 100.0);
}

double MultiArmedBandit::Strategy::getUCBValue(uint32_t totalVisits,
                                               double ucbConstant) const {
  if (visitCount == 0)
    return std::numeric_limits<double>::max();

  double exploration =
      ucbConstant * std::sqrt(std::log(totalVisits) / visitCount);
  return averageReward + exploration;
}

// MultiArmedBandit implementation
MultiArmedBandit::MultiArmedBandit(const MultiArmedBanditConfig& config)
    : m_config(config), m_rng(std::random_device{}()) {}

std::string MultiArmedBandit::selectStrategy(const MarketState& context) {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  if (m_strategies.empty()) {
    return "";
  }

  m_totalSelections++;

  // During warmup period, use random selection
  if (m_totalSelections < m_config.warmupPeriod) {
    std::vector<std::string> activeStrategies;
    for (const auto& [name, strategy] : m_strategies) {
      if (strategy.isActive) {
        activeStrategies.push_back(name);
      }
    }

    if (activeStrategies.empty())
      return "";

    std::uniform_int_distribution<size_t> dist(0, activeStrategies.size() - 1);
    return activeStrategies[dist(m_rng)];
  }

  // Use UCB for strategy selection
  std::string bestStrategy;
  double bestUCB = std::numeric_limits<double>::lowest();

  for (const auto& [name, strategy] : m_strategies) {
    if (!strategy.isActive)
      continue;

    double ucbValue =
        strategy.getUCBValue(m_totalSelections, m_config.ucbConstant);
    if (m_config.enableContextual) {
      ucbValue += calculateContextualReward(name, context);
    }

    if (ucbValue > bestUCB) {
      bestUCB = ucbValue;
      bestStrategy = name;
    }
  }

  return bestStrategy;
}

void MultiArmedBandit::updateReward(const std::string& strategy,
                                    double reward) {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  auto it = m_strategies.find(strategy);
  if (it != m_strategies.end()) {
    it->second.updateReward(reward, m_config.decayFactor);
  }
}

void MultiArmedBandit::addStrategy(const std::string& name) {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  Strategy strategy;
  strategy.name = name;
  m_strategies[name] = strategy;
}

void MultiArmedBandit::removeStrategy(const std::string& name) {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);
  m_strategies.erase(name);
}

void MultiArmedBandit::setStrategyActive(const std::string& name, bool active) {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  auto it = m_strategies.find(name);
  if (it != m_strategies.end()) {
    it->second.isActive = active;
  }
}

std::vector<MultiArmedBandit::Strategy>
MultiArmedBandit::getStrategies() const {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  std::vector<Strategy> result;
  for (const auto& [name, strategy] : m_strategies) {
    result.push_back(strategy);
  }
  return result;
}

MultiArmedBandit::Strategy MultiArmedBandit::getBestStrategy() const {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  Strategy best;
  double bestReward = std::numeric_limits<double>::lowest();

  for (const auto& [name, strategy] : m_strategies) {
    if (strategy.isActive && strategy.averageReward > bestReward) {
      bestReward = strategy.averageReward;
      best = strategy;
    }
  }

  return best;
}

std::string MultiArmedBandit::getStatistics() const {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  std::ostringstream oss;
  oss << "Multi-Armed Bandit Statistics:\n";
  oss << "  Total Strategies: " << m_strategies.size() << "\n";
  oss << "  Total Selections: " << m_totalSelections << "\n";
  oss << "  UCB Constant: " << m_config.ucbConstant << "\n";
  oss << "  Decay Factor: " << m_config.decayFactor << "\n";
  oss << "  Contextual Bandits: "
      << (m_config.enableContextual ? "Enabled" : "Disabled") << "\n";

  oss << "\nStrategy Performance:\n";
  for (const auto& [name, strategy] : m_strategies) {
    oss << "  " << name << ": avg=" << strategy.averageReward
        << ", visits=" << strategy.visitCount
        << ", confidence=" << strategy.confidence
        << ", active=" << (strategy.isActive ? "Yes" : "No") << "\n";
  }

  return oss.str();
}

void MultiArmedBandit::reset() {
  std::lock_guard<std::mutex> lock(m_strategiesMutex);

  for (auto& [name, strategy] : m_strategies) {
    strategy.averageReward = 0.0;
    strategy.visitCount = 0;
    strategy.rewardSum = 0.0;
    strategy.confidence = 0.0;
  }
  m_totalSelections = 0;
}

double
MultiArmedBandit::calculateContextualReward(const std::string& strategy,
                                            const MarketState& context) const {
  // Simple contextual reward calculation based on market conditions
  // This can be extended with more sophisticated contextual features

  double contextualBonus = 0.0;

  // High volatility favors conservative strategies
  if (context.volatility > 0.02) { // 2% volatility threshold
    if (strategy.find("conservative") != std::string::npos) {
      contextualBonus += 0.1;
    }
  }

  // High liquidity favors aggressive strategies
  if (context.liquidity > 0.8) {
    if (strategy.find("aggressive") != std::string::npos) {
      contextualBonus += 0.1;
    }
  }

  // Order book imbalance affects strategy selection
  if (std::abs(context.imbalance) > 0.3) {
    if (strategy.find("imbalance") != std::string::npos) {
      contextualBonus += 0.05;
    }
  }

  return contextualBonus;
}

// RLParameterAdapter Config implementation
RLParameterAdapter::Config::Config() {
  // Initialize default parameter constraints
  parameterConstraints = createDefaultParameterConstraints();
}

// RLParameterAdapter implementation
RLParameterAdapter::RLParameterAdapter(const std::string& symbol,
                                       const Config& config)
    : m_symbol(symbol), m_config(config) {

  // Initialize RL components
  m_qAgent = std::make_unique<QLearningAgent>(m_config.qLearningConfig);
  m_bandit = std::make_unique<MultiArmedBandit>(m_config.banditConfig);

  // Initialize parameter tracking
  for (const auto& [paramType, constraints] : m_config.parameterConstraints) {
    m_currentParameters[paramType] =
        (constraints.minValue + constraints.maxValue) / 2.0;
    m_parameterAdaptationEnabled[paramType] = true;
  }
}

RLParameterAdapter::~RLParameterAdapter() { stop(); }

bool RLParameterAdapter::initialize() {
  std::lock_guard<std::mutex> lock(m_stateMutex);

  // Add default strategies to bandit
  m_bandit->addStrategy("conservative");
  m_bandit->addStrategy("aggressive");
  m_bandit->addStrategy("balanced");
  m_bandit->addStrategy("momentum");
  m_bandit->addStrategy("mean_reversion");

  m_episodeStartTime = TimeUtils::getCurrentNanos();
  m_lastAdaptationTime = m_episodeStartTime;

  return true;
}

bool RLParameterAdapter::start() {
  if (m_isRunning.load()) {
    return false;
  }

  if (!initialize()) {
    return false;
  }

  m_isRunning.store(true);
  return true;
}

bool RLParameterAdapter::stop() {
  if (!m_isRunning.load()) {
    return false;
  }

  // Process final episode if needed
  if (isEpisodeComplete()) {
    processEpisodeEnd();
  }

  m_isRunning.store(false);
  return true;
}

void RLParameterAdapter::updateMarketState(const MarketState& state) {
  std::lock_guard<std::mutex> lock(m_stateMutex);

  m_previousState = m_currentState;
  m_currentState = state;
}

void RLParameterAdapter::recordPerformance(double pnl, double fillRate,
                                           double riskMetric,
                                           uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(m_performanceMutex);

  m_currentEpisodeMetrics.update(pnl, fillRate, riskMetric, timestamp);
}

void RLParameterAdapter::adaptParameters(strategy::StrategyConfig& config) {
  if (!m_isRunning.load())
    return;

  uint64_t currentTime = TimeUtils::getCurrentNanos();

  // Check if it's time for adaptation
  if (currentTime - m_lastAdaptationTime <
      m_config.adaptationIntervalMs * 1000000ULL) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_parameterMutex);

  MarketState currentState = extractMarketState();

  // Adapt each enabled parameter
  for (const auto& [paramType, enabled] : m_parameterAdaptationEnabled) {
    if (!enabled)
      continue;

    std::vector<ActionType> validActions = getValidActions(paramType);
    if (validActions.empty())
      continue;

    // Select action using Q-learning
    ActionType action =
        m_qAgent->selectAction(currentState, paramType, validActions);

    // Apply parameter update
    updateParameterFromAction(action, paramType, config);

    // Record action for later reward calculation
    ActionReward actionReward;
    actionReward.stateBefore = m_previousState;
    actionReward.action = action;
    actionReward.parameter = paramType;
    actionReward.parameterValue = m_currentParameters[paramType];
    actionReward.stateAfter = currentState;
    actionReward.timestamp = currentTime;
    actionReward.episodeId = m_currentEpisode.load();

    {
      std::lock_guard<std::mutex> actionLock(m_actionMutex);
      m_actionHistory.push_back(actionReward);
    }

    m_actionCount++;
  }

  m_lastAdaptationTime = currentTime;

  // Check if episode is complete
  if (isEpisodeComplete()) {
    processEpisodeEnd();
  }

  // Apply safety constraints
  applyParameterConstraints();
}

std::string RLParameterAdapter::selectStrategy(
    const std::vector<std::string>& availableStrategies) {
  if (availableStrategies.empty())
    return "";

  // Add new strategies to bandit if they don't exist
  for (const auto& strategy : availableStrategies) {
    m_bandit->addStrategy(strategy);
  }

  // Deactivate strategies not in available list
  auto allStrategies = m_bandit->getStrategies();
  for (const auto& strategy : allStrategies) {
    bool isAvailable =
        std::find(availableStrategies.begin(), availableStrategies.end(),
                  strategy.name) != availableStrategies.end();
    m_bandit->setStrategyActive(strategy.name, isAvailable);
  }

  MarketState currentState = extractMarketState();
  return m_bandit->selectStrategy(currentState);
}

void RLParameterAdapter::recordStrategyPerformance(const std::string& strategy,
                                                   double reward) {
  m_bandit->updateReward(strategy, reward);
}

void RLParameterAdapter::setParameterValue(ParameterType parameter,
                                           double value) {
  std::lock_guard<std::mutex> lock(m_parameterMutex);

  auto it = m_config.parameterConstraints.find(parameter);
  if (it != m_config.parameterConstraints.end()) {
    m_currentParameters[parameter] = it->second.clamp(value);
  } else {
    m_currentParameters[parameter] = value;
  }
}

double RLParameterAdapter::getParameterValue(ParameterType parameter) const {
  std::lock_guard<std::mutex> lock(m_parameterMutex);

  auto it = m_currentParameters.find(parameter);
  return it != m_currentParameters.end() ? it->second : 0.0;
}

bool RLParameterAdapter::isParameterAdaptationEnabled(
    ParameterType parameter) const {
  std::lock_guard<std::mutex> lock(m_parameterMutex);

  auto it = m_parameterAdaptationEnabled.find(parameter);
  return it != m_parameterAdaptationEnabled.end() ? it->second : false;
}

void RLParameterAdapter::enableParameterAdaptation(ParameterType parameter,
                                                   bool enable) {
  std::lock_guard<std::mutex> lock(m_parameterMutex);
  m_parameterAdaptationEnabled[parameter] = enable;
}

std::string RLParameterAdapter::getPerformanceStatistics() const {
  std::lock_guard<std::mutex> lock(m_performanceMutex);

  std::ostringstream oss;
  oss << "RL Parameter Adapter Performance:\n";
  oss << "  Current Episode: " << m_currentEpisode.load() << "\n";
  oss << "  Total Actions: " << m_actionCount.load() << "\n";
  oss << "  Episode History Size: " << m_episodeHistory.size() << "\n";

  oss << "\nCurrent Episode Metrics:\n";
  oss << "  Total P&L: " << m_currentEpisodeMetrics.totalPnL << "\n";
  oss << "  Average Fill Rate: "
      << (m_currentEpisodeMetrics.sampleCount > 0
              ? m_currentEpisodeMetrics.totalFillRate /
                    m_currentEpisodeMetrics.sampleCount
              : 0.0)
      << "\n";
  oss << "  Average Risk Metric: "
      << (m_currentEpisodeMetrics.sampleCount > 0
              ? m_currentEpisodeMetrics.totalRiskMetric /
                    m_currentEpisodeMetrics.sampleCount
              : 0.0)
      << "\n";
  oss << "  Sample Count: " << m_currentEpisodeMetrics.sampleCount << "\n";

  if (!m_episodeHistory.empty()) {
    double avgReward = 0.0;
    for (const auto& episode : m_episodeHistory) {
      avgReward += episode.calculateReward(m_config);
    }
    avgReward /= m_episodeHistory.size();
    oss << "\nHistorical Performance:\n";
    oss << "  Average Episode Reward: " << avgReward << "\n";
  }

  return oss.str();
}

std::string RLParameterAdapter::getQLearningStatistics() const {
  return m_qAgent->getStatistics();
}

std::string RLParameterAdapter::getBanditStatistics() const {
  return m_bandit->getStatistics();
}

void RLParameterAdapter::forceEpisodeEnd() {
  if (m_isRunning.load()) {
    processEpisodeEnd();
  }
}

uint32_t RLParameterAdapter::getCurrentEpisode() const {
  return m_currentEpisode.load();
}

uint64_t RLParameterAdapter::getActionCount() const {
  return m_actionCount.load();
}

bool RLParameterAdapter::saveModel(const std::string& filename) const {
  // Save Q-learning model
  std::string qTableFile = filename + "_qtable.bin";
  if (!m_qAgent->saveQTable(qTableFile)) {
    return false;
  }

  // Save configuration and state
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open())
    return false;

  // Save current parameters
  size_t paramCount = m_currentParameters.size();
  file.write(reinterpret_cast<const char*>(&paramCount), sizeof(paramCount));

  for (const auto& [param, value] : m_currentParameters) {
    file.write(reinterpret_cast<const char*>(&param), sizeof(param));
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  // Save episode and action counts
  uint32_t episode = m_currentEpisode.load();
  uint64_t actions = m_actionCount.load();
  file.write(reinterpret_cast<const char*>(&episode), sizeof(episode));
  file.write(reinterpret_cast<const char*>(&actions), sizeof(actions));

  return file.good();
}

bool RLParameterAdapter::loadModel(const std::string& filename) {
  // Load Q-learning model
  std::string qTableFile = filename + "_qtable.bin";
  if (!m_qAgent->loadQTable(qTableFile)) {
    return false;
  }

  // Load configuration and state
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open())
    return false;

  std::lock_guard<std::mutex> lock(m_parameterMutex);

  // Load current parameters
  size_t paramCount;
  file.read(reinterpret_cast<char*>(&paramCount), sizeof(paramCount));

  m_currentParameters.clear();
  for (size_t i = 0; i < paramCount; ++i) {
    ParameterType param;
    double value;
    file.read(reinterpret_cast<char*>(&param), sizeof(param));
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    m_currentParameters[param] = value;
  }

  // Load episode and action counts
  uint32_t episode;
  uint64_t actions;
  file.read(reinterpret_cast<char*>(&episode), sizeof(episode));
  file.read(reinterpret_cast<char*>(&actions), sizeof(actions));

  m_currentEpisode = episode;
  m_actionCount = actions;

  return file.good();
}

void RLParameterAdapter::reset() {
  std::lock_guard<std::mutex> stateLock(m_stateMutex);
  std::lock_guard<std::mutex> perfLock(m_performanceMutex);
  std::lock_guard<std::mutex> paramLock(m_parameterMutex);
  std::lock_guard<std::mutex> actionLock(m_actionMutex);

  m_qAgent->reset();
  m_bandit->reset();

  m_currentEpisode = 0;
  m_actionCount = 0;
  m_episodeStartTime = TimeUtils::getCurrentNanos();
  m_lastAdaptationTime = m_episodeStartTime;

  m_currentEpisodeMetrics.reset();
  m_episodeHistory.clear();
  m_actionHistory.clear();

  // Reset parameters to defaults
  for (const auto& [paramType, constraints] : m_config.parameterConstraints) {
    m_currentParameters[paramType] =
        (constraints.minValue + constraints.maxValue) / 2.0;
  }
}

bool RLParameterAdapter::updateConfig(const Config& config) {
  if (m_isRunning.load()) {
    return false; // Cannot update config while running
  }

  m_config = config;

  // Recreate RL components with new config
  m_qAgent = std::make_unique<QLearningAgent>(m_config.qLearningConfig);
  m_bandit = std::make_unique<MultiArmedBandit>(m_config.banditConfig);

  return true;
}

// Private helper methods
void RLParameterAdapter::processEpisodeEnd() {
  // Calculate episode reward
  double episodeReward = calculateReward(m_currentEpisodeMetrics);

  // Update Q-values for all actions in this episode
  {
    std::lock_guard<std::mutex> actionLock(m_actionMutex);
    for (auto& actionReward : m_actionHistory) {
      if (actionReward.episodeId == m_currentEpisode.load()) {
        actionReward.reward = episodeReward;

        m_qAgent->updateQValue(actionReward.stateBefore, actionReward.action,
                               actionReward.parameter, episodeReward,
                               actionReward.stateAfter);
      }
    }
  }

  // Store episode metrics
  {
    std::lock_guard<std::mutex> perfLock(m_performanceMutex);
    m_episodeHistory.push_back(m_currentEpisodeMetrics);
    if (m_episodeHistory.size() > m_config.maxHistorySize) {
      m_episodeHistory.pop_front();
    }
    m_currentEpisodeMetrics.reset();
  }

  // Start new episode
  m_currentEpisode++;
  m_episodeStartTime = TimeUtils::getCurrentNanos();

  // Cleanup old data
  cleanupOldData();
}

double
RLParameterAdapter::calculateReward(const PerformanceMetrics& metrics) const {
  if (metrics.sampleCount == 0)
    return 0.0;

  double pnlReward = calculatePnLReward(metrics.totalPnL);
  double fillRateReward =
      calculateFillRateReward(metrics.totalFillRate / metrics.sampleCount);
  double riskReward =
      calculateRiskReward(metrics.totalRiskMetric / metrics.sampleCount);
  double stabilityReward = calculateStabilityReward();

  return pnlReward * m_config.pnlRewardWeight +
         fillRateReward * m_config.fillRateRewardWeight +
         riskReward * m_config.riskRewardWeight +
         stabilityReward * m_config.stabilityRewardWeight;
}

void RLParameterAdapter::updateParameterFromAction(
    ActionType action, ParameterType parameter,
    strategy::StrategyConfig& config) {
  auto constraintIt = m_config.parameterConstraints.find(parameter);
  if (constraintIt == m_config.parameterConstraints.end())
    return;

  const auto& constraints = constraintIt->second;
  double currentValue = m_currentParameters[parameter];
  double newValue = currentValue;

  switch (action) {
  case ActionType::INCREASE_SPREAD:
  case ActionType::INCREASE_ORDER_SIZE:
  case ActionType::INCREASE_POSITION_LIMIT:
  case ActionType::INCREASE_RISK_TOLERANCE:
    newValue = constraints.getNextValue(currentValue, true);
    break;

  case ActionType::DECREASE_SPREAD:
  case ActionType::DECREASE_ORDER_SIZE:
  case ActionType::DECREASE_POSITION_LIMIT:
  case ActionType::DECREASE_RISK_TOLERANCE:
    newValue = constraints.getNextValue(currentValue, false);
    break;

  case ActionType::MAINTAIN_CURRENT:
  default:
    // No change
    break;
  }

  // Apply maximum change constraint
  double maxChange = m_config.maxParameterChangePerEpisode * currentValue;
  newValue = std::max(currentValue - maxChange,
                      std::min(currentValue + maxChange, newValue));

  m_currentParameters[parameter] = newValue;

  // Update strategy config based on parameter type
  switch (parameter) {
  case ParameterType::BASE_SPREAD_BPS:
    config.baseSpreadBps = static_cast<int>(newValue);
    break;
  case ParameterType::ORDER_QUANTITY:
    config.orderQuantity = newValue;
    break;
  case ParameterType::MAX_POSITION:
    config.maxPosition = newValue;
    break;
  case ParameterType::INVENTORY_SKEW_FACTOR:
    config.inventorySkewFactor = Factor(newValue);
    break;
  case ParameterType::ML_CONFIDENCE_THRESHOLD:
    // This would typically map to ML-specific config
    break;
  case ParameterType::FLOW_ADJUSTMENT_WEIGHT:
    // This would typically map to flow analysis config
    break;
  case ParameterType::IMPACT_ADJUSTMENT_WEIGHT:
    // This would typically map to impact prediction config
    break;
  }
}

std::vector<ActionType>
RLParameterAdapter::getValidActions(ParameterType parameter) const {
  auto constraintIt = m_config.parameterConstraints.find(parameter);
  if (constraintIt == m_config.parameterConstraints.end()) {
    return {ActionType::MAINTAIN_CURRENT};
  }

  const auto& constraints = constraintIt->second;
  double currentValue = m_currentParameters.at(parameter);

  std::vector<ActionType> validActions = {ActionType::MAINTAIN_CURRENT};

  // Check if we can increase
  if (currentValue < constraints.maxValue) {
    switch (parameter) {
    case ParameterType::BASE_SPREAD_BPS:
      validActions.push_back(ActionType::INCREASE_SPREAD);
      break;
    case ParameterType::ORDER_QUANTITY:
      validActions.push_back(ActionType::INCREASE_ORDER_SIZE);
      break;
    case ParameterType::MAX_POSITION:
      validActions.push_back(ActionType::INCREASE_POSITION_LIMIT);
      break;
    default:
      validActions.push_back(ActionType::INCREASE_RISK_TOLERANCE);
      break;
    }
  }

  // Check if we can decrease
  if (currentValue > constraints.minValue) {
    switch (parameter) {
    case ParameterType::BASE_SPREAD_BPS:
      validActions.push_back(ActionType::DECREASE_SPREAD);
      break;
    case ParameterType::ORDER_QUANTITY:
      validActions.push_back(ActionType::DECREASE_ORDER_SIZE);
      break;
    case ParameterType::MAX_POSITION:
      validActions.push_back(ActionType::DECREASE_POSITION_LIMIT);
      break;
    default:
      validActions.push_back(ActionType::DECREASE_RISK_TOLERANCE);
      break;
    }
  }

  return validActions;
}

MarketState RLParameterAdapter::extractMarketState() const {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  return m_currentState;
}

void RLParameterAdapter::cleanupOldData() {
  uint64_t currentTime = TimeUtils::getCurrentNanos();
  uint64_t cutoffTime = currentTime - (3600000000000ULL); // 1 hour

  std::lock_guard<std::mutex> actionLock(m_actionMutex);

  // Remove old action history
  m_actionHistory.erase(
      std::remove_if(m_actionHistory.begin(), m_actionHistory.end(),
                     [cutoffTime](const ActionReward& action) {
                       return action.timestamp < cutoffTime;
                     }),
      m_actionHistory.end());
}

bool RLParameterAdapter::isEpisodeComplete() const {
  uint64_t currentTime = TimeUtils::getCurrentNanos();
  uint64_t episodeDuration = currentTime - m_episodeStartTime;

  return episodeDuration >= m_config.episodeIntervalMs * 1000000ULL ||
         m_actionCount.load() >= m_config.minActionsPerEpisode;
}

void RLParameterAdapter::applyParameterConstraints() {
  std::lock_guard<std::mutex> lock(m_parameterMutex);

  for (auto& [param, value] : m_currentParameters) {
    auto constraintIt = m_config.parameterConstraints.find(param);
    if (constraintIt != m_config.parameterConstraints.end()) {
      value = constraintIt->second.clamp(value);
    }
  }
}

double RLParameterAdapter::calculatePnLReward(double pnl) const {
  // Normalize P&L reward to [-1, 1] range
  return std::tanh(pnl / 1000.0); // Assume $1000 is significant P&L
}

double RLParameterAdapter::calculateFillRateReward(double fillRate) const {
  // Fill rate should be high, reward linearly
  return std::max(0.0, std::min(1.0, fillRate)) * 2.0 -
         1.0; // Map [0,1] to [-1,1]
}

double RLParameterAdapter::calculateRiskReward(double riskMetric) const {
  // Lower risk is better, invert the metric
  return std::max(-1.0, 1.0 - riskMetric);
}

double RLParameterAdapter::calculateStabilityReward() const {
  // Reward stability in parameter values
  // This is a simplified implementation - could be enhanced with actual
  // stability metrics
  return 0.0;
}

// PerformanceMetrics implementation
void RLParameterAdapter::PerformanceMetrics::update(double pnl, double fillRate,
                                                    double risk,
                                                    uint64_t timestamp) {
  totalPnL += pnl;
  totalFillRate += fillRate;
  totalRiskMetric += risk;
  sampleCount++;
  lastUpdate = timestamp;
}

double RLParameterAdapter::PerformanceMetrics::calculateReward(
    const Config& config) const {
  if (sampleCount == 0)
    return 0.0;

  double avgFillRate = totalFillRate / sampleCount;
  double avgRisk = totalRiskMetric / sampleCount;

  double pnlReward = std::tanh(totalPnL / 1000.0);
  double fillReward = avgFillRate * 2.0 - 1.0;
  double riskReward = 1.0 - avgRisk;
  double stabilityReward = 0.0; // Simplified

  return pnlReward * config.pnlRewardWeight +
         fillReward * config.fillRateRewardWeight +
         riskReward * config.riskRewardWeight +
         stabilityReward * config.stabilityRewardWeight;
}

void RLParameterAdapter::PerformanceMetrics::reset() {
  totalPnL = 0.0;
  totalFillRate = 0.0;
  totalRiskMetric = 0.0;
  sampleCount = 0;
  lastUpdate = 0;
}

// Helper functions
std::string parameterTypeToString(ParameterType type) {
  switch (type) {
  case ParameterType::BASE_SPREAD_BPS:
    return "BASE_SPREAD_BPS";
  case ParameterType::ORDER_QUANTITY:
    return "ORDER_QUANTITY";
  case ParameterType::MAX_POSITION:
    return "MAX_POSITION";
  case ParameterType::INVENTORY_SKEW_FACTOR:
    return "INVENTORY_SKEW_FACTOR";
  case ParameterType::ML_CONFIDENCE_THRESHOLD:
    return "ML_CONFIDENCE_THRESHOLD";
  case ParameterType::FLOW_ADJUSTMENT_WEIGHT:
    return "FLOW_ADJUSTMENT_WEIGHT";
  case ParameterType::IMPACT_ADJUSTMENT_WEIGHT:
    return "IMPACT_ADJUSTMENT_WEIGHT";
  default:
    return "UNKNOWN";
  }
}

std::string actionTypeToString(ActionType type) {
  switch (type) {
  case ActionType::INCREASE_SPREAD:
    return "INCREASE_SPREAD";
  case ActionType::DECREASE_SPREAD:
    return "DECREASE_SPREAD";
  case ActionType::INCREASE_ORDER_SIZE:
    return "INCREASE_ORDER_SIZE";
  case ActionType::DECREASE_ORDER_SIZE:
    return "DECREASE_ORDER_SIZE";
  case ActionType::INCREASE_POSITION_LIMIT:
    return "INCREASE_POSITION_LIMIT";
  case ActionType::DECREASE_POSITION_LIMIT:
    return "DECREASE_POSITION_LIMIT";
  case ActionType::INCREASE_RISK_TOLERANCE:
    return "INCREASE_RISK_TOLERANCE";
  case ActionType::DECREASE_RISK_TOLERANCE:
    return "DECREASE_RISK_TOLERANCE";
  case ActionType::MAINTAIN_CURRENT:
    return "MAINTAIN_CURRENT";
  default:
    return "UNKNOWN";
  }
}

std::unordered_map<ParameterType, ParameterConstraints>
createDefaultParameterConstraints() {
  return {{ParameterType::BASE_SPREAD_BPS,
           ParameterConstraints(1.0, 50.0, 1.0, true)},
          {ParameterType::ORDER_QUANTITY,
           ParameterConstraints(0.01, 10.0, 0.01, false)},
          {ParameterType::MAX_POSITION,
           ParameterConstraints(1.0, 100.0, 1.0, false)},
          {ParameterType::INVENTORY_SKEW_FACTOR,
           ParameterConstraints(0.1, 2.0, 0.1, false)},
          {ParameterType::ML_CONFIDENCE_THRESHOLD,
           ParameterConstraints(0.1, 0.9, 0.05, false)},
          {ParameterType::FLOW_ADJUSTMENT_WEIGHT,
           ParameterConstraints(0.0, 1.0, 0.1, false)},
          {ParameterType::IMPACT_ADJUSTMENT_WEIGHT,
           ParameterConstraints(0.0, 1.0, 0.1, false)}};
}

} // namespace rl
} // namespace pinnacle
