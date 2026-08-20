# JSON Data Export

PinnacleMM provides comprehensive structured data export capabilities through JSON Lines (JSONL) format logging. This feature enables detailed analysis, backtesting, monitoring, and debugging of trading strategies and market data.

## Features

- **Market Data Logging**: Real-time price, volume, bid/ask data with timestamps
- **Strategy Metrics**: Position, P&L, quote counts, and performance statistics
- **Order Book Updates**: Complete order book state with bid and ask arrays
- **Connection Events**: WebSocket connections, disconnections, and errors
- **Trading Events**: Order placements, fills, cancellations, and status updates
- **Thread-Safe**: Concurrent logging without performance impact
- **JSONL Format**: One JSON object per line for easy parsing and streaming

## Usage

Enable JSON logging with the `--json-log` flag and optionally specify a custom file path:

```bash
# Basic JSON logging (default file: pinnaclemm_data.jsonl)
./pinnaclemm --mode simulation --symbol BTC-USD --json-log

# Custom file path
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --json-log --json-log-file my_trading_data.jsonl

# Combined with other features
./pinnaclemm --mode live --enable-ml --enable-visualization --json-log --json-log-file full_session.jsonl
```

## Sample Output

```json
{"format":"jsonl","timestamp":"2025-09-29T16:27:06.770Z","type":"session_start","version":"1.0.0"}
{"metrics":{"ask_price":67252.80,"bid_price":67248.20,"market_price":67250.45,"pnl":0.0,"position":0.0,"quote_updates":1,"strategy_name":"BasicMarketMaker","volume":1234.56},"strategy_name":"BasicMarketMaker","symbol":"BTC-USD","timestamp":"2025-09-29T16:27:12.011Z","type":"strategy_metrics"}
{"ask_price":67253.00,"bid_price":67249.50,"event_timestamp":1695736032610,"is_buy":true,"price":67250.75,"symbol":"BTC-USD","timestamp":"2025-09-29T16:27:12.610Z","type":"market_update","volume":0.5}
```

## Data Types

- **`session_start`**: Session initialization marker with format and version
- **`strategy_metrics`**: Trading strategy performance and position data
- **`market_update`**: Real-time market data from exchange feeds
- **`order_book_update`**: Complete order book snapshots with bid/ask arrays
- **`trading_event`**: Order lifecycle events and trading actions
- **`connection_event`**: Exchange connectivity status and errors

## File Management

JSON log files are created in the current working directory by default. For production use, consider:

```bash
# Save to logs directory (create and add to .gitignore)
mkdir -p logs
./pinnaclemm --json-log --json-log-file logs/trading_$(date +%Y%m%d_%H%M%S).jsonl

# Save to data directory (existing, likely gitignored)
./pinnaclemm --json-log --json-log-file data/market_data_$(date +%Y%m%d).jsonl

# Save outside project directory
./pinnaclemm --json-log --json-log-file ~/trading_logs/pinnaclemm_session.jsonl
```
