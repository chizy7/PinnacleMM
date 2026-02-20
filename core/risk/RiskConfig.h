#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace pinnacle {
namespace risk {

/**
 * @struct RiskLimits
 * @brief Position and exposure limits for risk management
 */
struct RiskLimits {
  // Position limits
  double maxPositionSize{10.0};
  double maxNotionalExposure{1000000.0};
  double maxNetExposure{500000.0};
  double maxGrossExposure{2000000.0};

  // Loss limits
  double maxDrawdownPct{5.0};
  double dailyLossLimit{10000.0};

  // Order-level limits
  double maxOrderSize{1.0};
  double maxOrderValue{50000.0};
  double maxDailyVolume{100.0};

  // Auto-hedge parameters
  bool autoHedgeEnabled{false};
  double hedgeThresholdPct{50.0};
  uint64_t hedgeIntervalMs{5000};

  // Rate limiting
  uint32_t maxOrdersPerSecond{100};
};

/**
 * @struct CircuitBreakerConfig
 * @brief Configuration for the circuit breaker
 */
struct CircuitBreakerConfig {
  // Price move thresholds
  double priceMove1minPct{2.0};
  double priceMove5minPct{5.0};

  // Spread threshold
  double spreadWidenMultiplier{3.0};

  // Volume spike threshold
  double volumeSpikeMultiplier{5.0};

  // Timing
  uint64_t cooldownPeriodMs{30000};
  uint64_t halfOpenTestDurationMs{10000};

  // Latency threshold
  uint64_t maxLatencyUs{10000};

  // Ring buffer size for price history
  size_t priceHistorySize{300};
};

/**
 * @struct VaRConfig
 * @brief Configuration for Value at Risk calculations
 */
struct VaRConfig {
  size_t windowSize{252};
  size_t simulationCount{10000};
  double horizon{1.0};
  uint64_t updateIntervalMs{60000};
  double confidenceLevel95{0.95};
  double confidenceLevel99{0.99};
  double varLimitPct{2.0};
};

/**
 * @struct AlertConfig
 * @brief Configuration for alert management
 */
struct AlertConfig {
  uint64_t minAlertIntervalMs{5000};
  size_t maxAlertHistory{1000};
  double warningThresholdPct{80.0};
  double criticalThresholdPct{100.0};
};

/**
 * @struct RiskConfig
 * @brief Unified risk configuration
 */
struct RiskConfig {
  RiskLimits limits;
  CircuitBreakerConfig circuitBreaker;
  VaRConfig var;
  AlertConfig alerts;

  /**
   * @brief Load risk configuration from JSON
   */
  static RiskConfig fromJson(const nlohmann::json& j) {
    RiskConfig config;

    if (j.contains("risk_management")) {
      const auto& rm = j["risk_management"];

      if (rm.contains("limits")) {
        const auto& lim = rm["limits"];
        config.limits.maxPositionSize =
            lim.value("max_position_size", config.limits.maxPositionSize);
        config.limits.maxNotionalExposure = lim.value(
            "max_notional_exposure", config.limits.maxNotionalExposure);
        config.limits.maxNetExposure =
            lim.value("max_net_exposure", config.limits.maxNetExposure);
        config.limits.maxGrossExposure =
            lim.value("max_gross_exposure", config.limits.maxGrossExposure);
        config.limits.maxDrawdownPct =
            lim.value("max_drawdown_pct", config.limits.maxDrawdownPct);
        config.limits.dailyLossLimit =
            lim.value("daily_loss_limit", config.limits.dailyLossLimit);
        config.limits.maxOrderSize =
            lim.value("max_order_size", config.limits.maxOrderSize);
        config.limits.maxOrderValue =
            lim.value("max_order_value", config.limits.maxOrderValue);
        config.limits.maxDailyVolume =
            lim.value("max_daily_volume", config.limits.maxDailyVolume);
        config.limits.maxOrdersPerSecond = lim.value(
            "max_orders_per_second", config.limits.maxOrdersPerSecond);
      }

      if (rm.contains("circuit_breaker")) {
        const auto& cb = rm["circuit_breaker"];
        config.circuitBreaker.priceMove1minPct = cb.value(
            "price_move_1min_pct", config.circuitBreaker.priceMove1minPct);
        config.circuitBreaker.priceMove5minPct = cb.value(
            "price_move_5min_pct", config.circuitBreaker.priceMove5minPct);
        config.circuitBreaker.spreadWidenMultiplier =
            cb.value("spread_widen_multiplier",
                     config.circuitBreaker.spreadWidenMultiplier);
        config.circuitBreaker.volumeSpikeMultiplier =
            cb.value("volume_spike_multiplier",
                     config.circuitBreaker.volumeSpikeMultiplier);
        config.circuitBreaker.cooldownPeriodMs = cb.value(
            "cooldown_period_ms", config.circuitBreaker.cooldownPeriodMs);
        config.circuitBreaker.halfOpenTestDurationMs =
            cb.value("half_open_test_duration_ms",
                     config.circuitBreaker.halfOpenTestDurationMs);
        config.circuitBreaker.maxLatencyUs =
            cb.value("max_latency_us", config.circuitBreaker.maxLatencyUs);
        config.circuitBreaker.priceHistorySize = cb.value(
            "price_history_size", config.circuitBreaker.priceHistorySize);
      }

      if (rm.contains("var")) {
        const auto& v = rm["var"];
        config.var.windowSize = v.value("window_size", config.var.windowSize);
        config.var.simulationCount =
            v.value("simulation_count", config.var.simulationCount);
        config.var.horizon = v.value("horizon", config.var.horizon);
        config.var.updateIntervalMs =
            v.value("update_interval_ms", config.var.updateIntervalMs);
        config.var.varLimitPct =
            v.value("var_limit_pct", config.var.varLimitPct);
      }

      if (rm.contains("auto_hedge")) {
        const auto& ah = rm["auto_hedge"];
        config.limits.autoHedgeEnabled =
            ah.value("enabled", config.limits.autoHedgeEnabled);
        config.limits.hedgeThresholdPct =
            ah.value("threshold_pct", config.limits.hedgeThresholdPct);
        config.limits.hedgeIntervalMs =
            ah.value("interval_ms", config.limits.hedgeIntervalMs);
      }

      if (rm.contains("alerts")) {
        const auto& al = rm["alerts"];
        config.alerts.minAlertIntervalMs =
            al.value("min_interval_ms", config.alerts.minAlertIntervalMs);
        config.alerts.maxAlertHistory =
            al.value("max_history", config.alerts.maxAlertHistory);
        config.alerts.warningThresholdPct = al.value(
            "warning_threshold_pct", config.alerts.warningThresholdPct);
        config.alerts.criticalThresholdPct = al.value(
            "critical_threshold_pct", config.alerts.criticalThresholdPct);
      }
    }

    return config;
  }

  /**
   * @brief Serialize to JSON
   */
  nlohmann::json toJson() const {
    return {
        {"risk_management",
         {{"limits",
           {{"max_position_size", limits.maxPositionSize},
            {"max_notional_exposure", limits.maxNotionalExposure},
            {"max_net_exposure", limits.maxNetExposure},
            {"max_gross_exposure", limits.maxGrossExposure},
            {"max_drawdown_pct", limits.maxDrawdownPct},
            {"daily_loss_limit", limits.dailyLossLimit},
            {"max_order_size", limits.maxOrderSize},
            {"max_order_value", limits.maxOrderValue},
            {"max_daily_volume", limits.maxDailyVolume},
            {"max_orders_per_second", limits.maxOrdersPerSecond}}},
          {"circuit_breaker",
           {{"price_move_1min_pct", circuitBreaker.priceMove1minPct},
            {"price_move_5min_pct", circuitBreaker.priceMove5minPct},
            {"spread_widen_multiplier", circuitBreaker.spreadWidenMultiplier},
            {"volume_spike_multiplier", circuitBreaker.volumeSpikeMultiplier},
            {"cooldown_period_ms", circuitBreaker.cooldownPeriodMs},
            {"half_open_test_duration_ms",
             circuitBreaker.halfOpenTestDurationMs},
            {"max_latency_us", circuitBreaker.maxLatencyUs},
            {"price_history_size", circuitBreaker.priceHistorySize}}},
          {"var",
           {{"window_size", var.windowSize},
            {"simulation_count", var.simulationCount},
            {"horizon", var.horizon},
            {"update_interval_ms", var.updateIntervalMs},
            {"var_limit_pct", var.varLimitPct}}},
          {"auto_hedge",
           {{"enabled", limits.autoHedgeEnabled},
            {"threshold_pct", limits.hedgeThresholdPct},
            {"interval_ms", limits.hedgeIntervalMs}}},
          {"alerts",
           {{"min_interval_ms", alerts.minAlertIntervalMs},
            {"max_history", alerts.maxAlertHistory},
            {"warning_threshold_pct", alerts.warningThresholdPct},
            {"critical_threshold_pct", alerts.criticalThresholdPct}}}}}};
  }
};

} // namespace risk
} // namespace pinnacle
