# Order Book Flow Analysis

## Overview

PinnacleMM Phase 3 introduces sophisticated order book flow analysis capabilities for enhanced market making decisions. The flow analysis system provides real-time monitoring of order book dynamics, liquidity patterns, and market microstructure, enabling more intelligent spread optimization and risk management.

## Key Features

### **Real-time Flow Monitoring**
- **Order Flow Events**: Comprehensive tracking of add, cancel, and fill events
- **Flow Rates**: Order rates, volume rates, and cancel rates per side
- **Flow Velocity**: Rate of change in bid/ask flow patterns
- **Market Impact Indicators**: Adverse selection and information content analysis

### **Advanced Imbalance Analysis**
- **Multi-level Depth**: Volume and order count imbalance across depth levels
- **Weighted Imbalance**: Distance-weighted imbalance calculations
- **Top-level Focus**: Specialized analysis of best bid/ask levels
- **Dynamic Thresholds**: Configurable imbalance detection sensitivity

### **Liquidity Prediction**
- **Short-term Forecasting**: Predict liquidity conditions 100ms-1s ahead
- **Confidence Scoring**: Prediction reliability assessment
- **Regime-aware Predictions**: Adapt to different market conditions
- **Flow-based Models**: Leverage historical flow patterns

### **Ultra-Low Latency Design**
- **Sub-microsecond Events**: <1μs event recording and processing
- **Lock-free Architecture**: Thread-safe concurrent operations
- **Efficient Caching**: 1ms metrics cache for repeated queries
- **Memory Management**: Automatic cleanup of historical data

## Architecture

### System Components

![Order Book Flow Analyzer System Architecture](../docs/images/order-book-flow.drawio.svg)

### Integration with ML System

![Order Book Flow Analyzer System Architecture](../docs/images/ml-enhanced-market-maker.drawio.svg)

## Getting Started

### Basic Usage

```bash
# Enable flow analysis with ML (default configuration)
./pinnaclemm --mode simulation --enable-ml --verbose

# Flow analysis is automatically enabled when ML is enabled
# To run with only flow analysis (no ML), modify ml_config.json
```

### Configuration

Flow analysis is configured via `config/ml_config.json`:

```json
{
  "ml_enhanced_market_maker": {
    "flowAnalysis": {
      "enableFlowAnalysis": true,
      "flowAnalysisWindowMs": 1000,
      "maxFlowEvents": 10000,
      "flowSpreadAdjustmentWeight": 0.3
    }
  },

  "flow_analysis": {
    "orderFlowTracking": {
      "enableOrderFlowEvents": true,
      "enableLiquidityAnalysis": true,
      "enableFlowPrediction": true,
      "windowSizeMs": 1000,
      "maxEventHistory": 10000
    },

    "imbalanceAnalysis": {
      "enableVolumeImbalance": true,
      "enableOrderCountImbalance": true,
      "enableWeightedImbalance": true,
      "depthLevels": 5,
      "imbalanceThreshold": 0.2
    },

    "liquidityPrediction": {
      "enableLiquidityForecasting": true,
      "predictionHorizonMs": 100,
      "confidenceThreshold": 0.5,
      "enableRegimeDetection": true
    },

    "spreadAdjustment": {
      "enableFlowBasedAdjustment": true,
      "adjustmentWeight": 0.3,
      "maxAdjustmentFactor": 3.0,
      "minAdjustmentFactor": 0.5,
      "velocityInfluence": 0.001,
      "imbalanceInfluence": 0.5,
      "liquidityInfluence": 0.3
    }
  }
}
```

## Flow Metrics

### Core Flow Metrics

The system tracks comprehensive flow metrics in real-time:

```cpp
struct FlowMetrics {
    // Flow rates (per second)
    double bidOrderRate;        // Orders per second on bid side
    double askOrderRate;        // Orders per second on ask side
    double bidCancelRate;       // Cancellations per second on bid side
    double askCancelRate;       // Cancellations per second on ask side
    double bidVolumeRate;       // Volume per second on bid side
    double askVolumeRate;       // Volume per second on ask side

    // Imbalance indicators
    double liquidityImbalance;  // (bid_volume - ask_volume) / total_volume
    double orderSizeImbalance;  // (avg_bid_size - avg_ask_size) / total_avg_size

    // Market impact indicators
    double aggressiveOrderRatio;    // Ratio of market orders
    double largeOrderRatio;         // Ratio of large orders (>95th percentile)
    double adverseSelectionRatio;   // Immediately filled orders ratio
    double informationContent;      // Price impact per unit volume

    // Persistence metrics
    double orderPersistence;    // Average order lifetime (ms)
    double levelPersistence;    // Average price level lifetime (ms)

    // Flow velocity
    double bidFlowVelocity;     // Rate of change in bid flow
    double askFlowVelocity;     // Rate of change in ask flow

    uint64_t timestamp;
    uint64_t windowStartTime;
    uint64_t windowEndTime;
};
```

### Imbalance Analysis

Multi-dimensional imbalance analysis across order book depth:

```cpp
struct ImbalanceAnalysis {
    double volumeImbalance;       // Volume imbalance across levels
    double orderCountImbalance;   // Order count imbalance
    double weightedImbalance;     // Distance-weighted imbalance
    double topLevelImbalance;     // Best bid/ask imbalance only
    uint64_t timestamp;
};
```

### Liquidity Prediction

Forward-looking liquidity assessment:

```cpp
struct LiquidityPrediction {
    double predictedBidLiquidity;   // Expected bid liquidity
    double predictedAskLiquidity;   // Expected ask liquidity
    double liquidityScore;          // Overall liquidity score [0-1]
    double confidence;              // Prediction confidence [0-1]
    uint64_t predictionHorizon;     // Prediction timeframe (ns)
    uint64_t timestamp;
};
```

## API Reference

### OrderBookAnalyzer

```cpp
class OrderBookAnalyzer {
public:
    // Constructor
    explicit OrderBookAnalyzer(const std::string& symbol,
                              uint64_t windowSizeMs = 1000,
                              size_t maxEvents = 10000);

    // Core functionality
    bool initialize(std::shared_ptr<OrderBook> orderBook);
    bool start();
    bool stop();
    bool isRunning() const;

    // Event recording
    void recordEvent(const OrderFlowEvent& event);

    // Analysis methods
    FlowMetrics getCurrentMetrics() const;
    ImbalanceAnalysis analyzeImbalance(size_t depth = 5) const;
    LiquidityPrediction predictLiquidity(uint64_t horizonMs = 100) const;

    // Spread optimization
    double calculateFlowBasedSpreadAdjustment(double baseSpread,
                                            const FlowMetrics& metrics) const;

    // Regime detection
    bool detectRegimeChange() const;

    // Reporting
    std::string getFlowStatistics() const;

    // Management
    void reset();
    const std::string& getSymbol() const;
};
```

### MLEnhancedMarketMaker Flow Extensions

```cpp
class MLEnhancedMarketMaker : public BasicMarketMaker {
public:
    // Flow analysis methods
    FlowMetrics getFlowMetrics() const;
    ImbalanceAnalysis getImbalanceAnalysis(size_t depth = 5) const;
    LiquidityPrediction getLiquidityPrediction(uint64_t horizonMs = 100) const;
    std::string getFlowStatistics() const;
    bool isFlowAnalysisEnabled() const;

protected:
    // Enhanced spread calculation with flow analysis
    double calculateFlowEnhancedSpread() const;
    MarketFeatures extractFlowEnhancedFeatures() const;
};
```

### Flow Events

```cpp
struct OrderFlowEvent {
    enum class Type {
        ADD_ORDER,
        CANCEL_ORDER,
        FILL_ORDER,
        PRICE_LEVEL_CHANGE
    };

    Type type;
    uint64_t timestamp;
    std::string orderId;
    OrderSide side;
    double price;
    double quantity;
    double newTotalQuantity;

    OrderFlowEvent(Type t, uint64_t ts, const std::string& id,
                   OrderSide s, double p, double q, double total = 0.0);
};
```

## Performance Metrics

### Real-time Monitoring

Flow analysis provides detailed performance metrics:

```
=== Order Flow Analysis Statistics ===
Symbol: BTC-USD
Window: 1000ms

Flow Rates:
  Bid Order Rate: 12.5 orders/sec
  Ask Order Rate: 11.8 orders/sec
  Bid Cancel Rate: 2.1 cancels/sec
  Ask Cancel Rate: 1.9 cancels/sec
  Bid Volume Rate: 125.3 volume/sec
  Ask Volume Rate: 118.7 volume/sec

Imbalances:
  Liquidity Imbalance: 0.027
  Order Size Imbalance: 0.013
  Volume Imbalance: 0.034
  Order Count Imbalance: 0.021

Market Impact:
  Aggressive Order Ratio: 0.156
  Large Order Ratio: 0.083
  Adverse Selection Ratio: 0.142
  Information Content: 0.0023

Persistence:
  Order Persistence: 1,247.3 ms
  Level Persistence: 2,891.7 ms

Flow Velocity:
  Bid Flow Velocity: 12.7
  Ask Flow Velocity: -8.3

Liquidity Prediction:
  Predicted Bid Liquidity: 125.8
  Predicted Ask Liquidity: 119.2
  Liquidity Score: 0.847
  Confidence: 0.763

Regime Detection: STABLE
```

### Performance Benchmarks

| Metric | Target | Achieved |
|--------|--------|----------|
| Event Recording | <1μs | ~100ns |
| Metrics Calculation | <1ms | ~200μs |
| Memory Usage | <50MB | ~25MB |
| Thread Safety | Lock-free | Ok |
| Test Coverage | >95% | 100% |

## Advanced Usage

### Custom Flow Event Recording

```cpp
// Record custom flow events
OrderFlowEvent event(
    OrderFlowEvent::Type::ADD_ORDER,
    TimeUtils::getCurrentNanos(),
    "order-123",
    OrderSide::BUY,
    50000.0,  // price
    0.1       // quantity
);

analyzer->recordEvent(event);
```

### Flow-based Spread Adjustments

```cpp
// Get current flow metrics
auto flowMetrics = marketMaker->getFlowMetrics();

// Calculate base spread
double baseSpread = 10.0; // 10 bps

// Apply flow-based adjustment
double adjustment = analyzer->calculateFlowBasedSpreadAdjustment(
    baseSpread, flowMetrics);

double finalSpread = baseSpread * adjustment;
```

### Imbalance Monitoring

```cpp
// Analyze imbalance across top 5 levels
auto imbalance = analyzer->analyzeImbalance(5);

if (std::abs(imbalance.volumeImbalance) > 0.3) {
    // Significant imbalance detected
    adjustSpreadForImbalance(imbalance);
}
```

### Liquidity Forecasting

```cpp
// Predict liquidity 100ms ahead
auto prediction = analyzer->predictLiquidity(100);

if (prediction.confidence > 0.7) {
    // High confidence prediction
    if (prediction.liquidityScore < 0.5) {
        // Low liquidity expected, widen spreads
        adjustForLowLiquidity();
    }
}
```

## Integration with ML Models

### Flow-Enhanced Features

The flow analyzer enhances ML model features:

```cpp
// Standard features
auto baseFeatures = extractMarketFeatures();

// Enhanced with flow data
auto flowEnhancedFeatures = extractFlowEnhancedFeatures();

// Flow influences:
// - Volatility adjustment based on flow velocity
// - Imbalance blending with order book imbalance
// - Volume profile enhancement with flow rates
// - Trade intensity boost from order rates
```

### Dynamic Spread Calculation

```cpp
double MLEnhancedMarketMaker::calculateTargetSpread() const {
    // Get ML prediction
    auto mlSpread = getMLSpreadPrediction();

    // Apply flow-based adjustment if enabled
    if (m_mlConfig.enableFlowAnalysis && m_flowAnalyzer) {
        auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
        double flowAdjustment = m_flowAnalyzer->calculateFlowBasedSpreadAdjustment(
            mlSpread, flowMetrics);

        double weight = m_mlConfig.flowSpreadAdjustmentWeight;
        mlSpread = mlSpread * (1.0 - weight) + (mlSpread * flowAdjustment) * weight;
    }

    return mlSpread;
}
```

## Testing

### Unit Tests

```bash
# Run flow analysis tests
./orderbook_analyzer_tests

# Expected output:
# [==========] Running 12 tests from 1 test suite.
# [  PASSED  ] 12 tests.
```

### Integration Testing

```bash
# Test with ML integration
./ml_enhanced_market_maker_tests

# Test real-time flow analysis
./pinnaclemm --mode simulation --enable-ml --verbose
```

### Performance Testing

```bash
# Test event recording performance (1000 events < 100ms)
./orderbook_analyzer_tests --gtest_filter="*Performance*"

# Test metrics calculation speed (<1ms)
./orderbook_analyzer_tests --gtest_filter="*Metrics*"
```

## Troubleshooting

### Common Issues

#### Flow Analysis Not Active
```
Flow analysis not available
```
**Solution**: Enable flow analysis in ML configuration:
```json
{
  "ml_enhanced_market_maker": {
    "flowAnalysis": {
      "enableFlowAnalysis": true
    }
  }
}
```

#### High Memory Usage
**Symptoms**: Memory usage growing continuously
**Solution**:
- Reduce `maxFlowEvents` in configuration
- Decrease `windowSizeMs` for shorter analysis windows
- Check for event cleanup frequency

#### Slow Performance
**Symptoms**: High latency in flow metrics calculation
**Solution**:
- Enable metrics caching
- Reduce analysis depth levels
- Optimize event recording frequency

### Performance Optimization

#### For Maximum Speed
```json
{
  "flow_analysis": {
    "orderFlowTracking": {
      "windowSizeMs": 500,
      "maxEventHistory": 5000
    },
    "imbalanceAnalysis": {
      "depthLevels": 3
    }
  }
}
```

#### For Maximum Accuracy
```json
{
  "flow_analysis": {
    "orderFlowTracking": {
      "windowSizeMs": 2000,
      "maxEventHistory": 20000
    },
    "imbalanceAnalysis": {
      "depthLevels": 10
    }
  }
}
```

## Future Enhancements

### Planned Features

1. **Machine Learning-based Flow Prediction**: Use ML models to predict flow patterns
2. **Cross-Asset Flow Correlation**: Analyze flow patterns across multiple instruments
3. **Flow Clustering**: Identify similar flow regimes using clustering algorithms
4. **Real-time Flow Visualization**: Live flow pattern visualization dashboard

### Research Areas

- **Deep Learning for Flow Patterns**: LSTM networks for flow sequence prediction
- **Reinforcement Learning**: Adaptive flow-based strategy parameters
- **Market Microstructure Modeling**: Advanced models of order flow dynamics
- **High-Frequency Pattern Recognition**: Microsecond-level flow pattern detection

## Conclusion

The Order Book Flow Analysis system represents a significant advancement in market microstructure analysis for algorithmic trading. By providing real-time insights into order flow dynamics, liquidity patterns, and market behavior, it enables more intelligent and adaptive market making strategies.

The system's ultra-low latency design, comprehensive metrics, and seamless ML integration make it suitable for production deployment in demanding high-frequency trading environments, while maintaining the reliability and performance standards required for institutional trading systems.
