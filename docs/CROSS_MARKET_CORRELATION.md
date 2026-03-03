# Cross-Market Correlation Analysis

## Overview

The `CrossMarketCorrelation` engine provides statistical models to detect when one instrument's price movement predicts another's. It implements four analysis methods:

1. **Pearson Correlation**: Full-sample linear correlation of log returns
2. **Rolling Correlation**: Sliding-window correlation for regime sensitivity
3. **Lead-Lag Analysis**: Detects if one instrument's returns predict another's with a time offset
4. **Engle-Granger Cointegration**: Tests whether two price series share a long-run equilibrium

## Integration with MLEnhancedMarketMaker

When cross-market signals are active, the `MLEnhancedMarketMaker` adjusts its spread based on the expected move magnitude and confidence from the leading instrument:

```cpp
// Wire up in main.cpp or strategy initialization
CrossMarketCorrelation crossMarket(config);
crossMarket.addPair("BTC-USD", "ETH-USD");

mlStrategy.setCrossMarketCorrelation(&crossMarket);
```

The spread adjustment widens when a correlated leader instrument shows a large expected move, protecting against adverse selection.

## Configuration

```cpp
struct CrossMarketConfig {
    size_t returnWindowSize{100};     // Window for return calculations
    size_t rollingWindowSize{30};     // Rolling correlation window
    int maxLagBars{10};               // Max lead-lag offset to test
    double minCorrelation{0.5};       // Min |correlation| to consider
    double signalThreshold{0.3};      // Min signal strength to emit
    double cointegrationPValue{0.05}; // Significance level
};
```

In `MLEnhancedMarketMaker::MLConfig`:

```cpp
bool enableCrossMarketSignals{false};
double crossMarketSpreadAdjustmentWeight{0.2};
```

## Statistical Methods

### Pearson Correlation

Computed on log returns (not raw prices) to improve stationarity (formal tests like ADF may still be needed):

```
r_i = log(P_i / P_{i-1})
corr(A, B) = Cov(r_A, r_B) / (Std(r_A) * Std(r_B))
```

Range: [-1, 1]. Values near +1 indicate co-movement; near -1 indicate inverse movement.

### Rolling Correlation

Same as Pearson but computed over the most recent `rollingWindowSize` returns. This captures regime-dependent correlation shifts.

### Lead-Lag Analysis

For each lag offset `k` in `[-maxLagBars, +maxLagBars]`, compute:

```
corr(r_A[t], r_B[t+k])
```

The lag with the highest absolute correlation is reported. Positive `leadLagBarsA` means A leads B.

### Engle-Granger Cointegration

1. Regress prices: `P_A = alpha + beta * P_B + epsilon`
2. Compute residuals `e_t = P_A_t - alpha - beta * P_B_t`
3. Run simplified ADF test on residuals: `delta_e_t = gamma * e_{t-1} + noise`
4. If t-statistic for gamma < -3.37 (5% critical value), the pair is cointegrated

Cointegrated pairs have a mean-reverting spread, making them candidates for pairs trading.

## Signal Generation

Signals are generated when:
1. A pair has `|leadLagCoefficient| >= minCorrelation`
2. The leader has a non-zero lag offset (`leadLagBarsA != 0`)
3. The leader's recent return multiplied by the coefficient exceeds `signalThreshold`

```cpp
struct CrossMarketSignal {
    std::string leadSymbol;       // The instrument that moves first
    std::string lagSymbol;        // The instrument expected to follow
    double signalStrength;        // [0, 1]
    double expectedMove;          // Expected % move in lag symbol
    double confidence;            // Based on rolling correlation
    uint64_t timestamp;
};
```

## Usage

```cpp
CrossMarketConfig cfg;
cfg.returnWindowSize = 100;
cfg.minCorrelation = 0.5;

CrossMarketCorrelation engine(cfg);
engine.addPair("BTC-USD", "ETH-USD");

// Feed price observations (from market data feeds)
engine.addPriceObservation("BTC-USD", 50000.0, 1000.0, timestampNs);
engine.addPriceObservation("ETH-USD", 3000.0, 5000.0, timestampNs);

// Query correlation
auto corr = engine.getCorrelation("BTC-USD", "ETH-USD");
// corr.pearsonCorrelation, corr.leadLagBarsA, corr.isCointegrated

// Get active trading signals
auto signals = engine.getActiveSignals();

// Get statistics
std::string stats = engine.getStatistics();
```

## Testing

```bash
cd build
./cross_market_correlation_tests    # 7 tests
```

Test cases:
- Perfectly correlated series (Pearson ~ 1.0)
- Inversely correlated series (Pearson < -0.8)
- Lead-lag detection with 2-bar offset
- Cointegration detection (linear relationship + noise)
- Uncorrelated data (low |correlation|)
- Signal generation from lead-lag patterns
- Statistics output
