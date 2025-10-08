# WebSocket Testing and Troubleshooting Guide

This guide provides comprehensive instructions for testing and troubleshooting WebSocket connections in the PinnacleMM visualization system.

## Overview

PinnacleMM includes a robust real-time visualization system built on Boost.Beast WebSocket servers. This system has been thoroughly tested and all known WebSocket segmentation fault issues have been resolved.

## Quick Testing

### 1. Test Dashboard

The easiest way to test WebSocket connectivity is using the built-in test dashboard:

```bash
# Start PinnacleMM with visualization
cd build
./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8089 --viz-api-port 8090

# Open test dashboard in browser
open test_dashboard.html
# or manually: file:///path/to/PinnacleMM/build/test_dashboard.html
```

The test dashboard provides:
- Real-time connection status
- Message history with timestamps
- Automatic subscription to performance data
- Connection/disconnection controls
- Error logging and diagnostics

### 2. Command Line Testing

You can also test WebSocket connections using command-line tools:

```bash
# Python test (recommended)
python3 -c "
import socket
import time

print('Testing WebSocket connection to localhost:8089...')
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    result = sock.connect_ex(('localhost', 8089))
    if result == 0:
        print('TCP connection successful!')

        # Send WebSocket handshake
        handshake = 'GET / HTTP/1.1\r\nHost: localhost:8089\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n'
        sock.send(handshake.encode())
        print('WebSocket handshake sent')

        # Try to receive response
        time.sleep(1)
        try:
            response = sock.recv(1024)
            print(f'Received response: {response[:100]}...')
        except Exception as e:
            print(f'Error receiving response: {e}')
    else:
        print(f'Connection failed: {result}')

    sock.close()
except Exception as e:
    print(f'Error: {e}')
"
```

## Running Multiple Instances

You can run multiple instances of PinnacleMM with different ports for testing:

```bash
# Terminal 1: Simulation mode
cd build
./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8089 --viz-api-port 8090

# Terminal 2: Another simulation instance
cd build
./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8091 --viz-api-port 8092

# Terminal 3: Live mode (requires credentials)
cd build
./pinnaclemm --mode live --exchange coinbase --enable-ml --enable-visualization --viz-ws-port 8085 --viz-api-port 8086
```

## Debug Mode

For detailed WebSocket debugging, enable debug logging:

```bash
# Enhanced debug logging
SPDLOG_LEVEL=debug ./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8089 --viz-api-port 8090 --verbose
```

This will show detailed logs including:
- WebSocket session creation
- Client connection/disconnection events
- Message sending/receiving
- Error conditions
- Performance data updates

## Expected Server Output

When the WebSocket server is running correctly, you should see:

```
[info] WebSocket server initialized on port 8089
[info] REST API server initialized on port 8090
[info] Visualization server started successfully
[debug] WebSocketHandler::broadcastLoop() started
[info] Visualization dashboard available at:
  WebSocket: ws://localhost:8089
  REST API: http://localhost:8090
  Dashboard: file://visualization/static/index.html
```

When a client connects successfully:

```
[debug] Creating new WebSocketSession
[debug] WebSocketSession::start() called
[info] WebSocket client connected. Total clients: 1
[debug] async_accept callback invoked with ec: Undefined error: 0
[info] WebSocket client connected
[info] Client subscribed to performance data for strategy primary_strategy
```

## Troubleshooting

### Connection Refused

If you see "Connection refused" errors:

1. **Check if PinnacleMM is running:**
   ```bash
   ps aux | grep pinnaclemm
   ```

2. **Verify ports are not in use:**
   ```bash
   lsof -i :8089
   lsof -i :8090
   ```

3. **Check firewall settings:**
   ```bash
   # macOS
   sudo pfctl -sr | grep 8089

   # Linux
   sudo iptables -L | grep 8089
   ```

### WebSocket Handshake Failures

If the WebSocket handshake fails:

1. **Check browser console** for detailed error messages
2. **Verify WebSocket URL** format: `ws://localhost:8089` (not `wss://`)
3. **Test with different browsers** (Chrome, Firefox, Safari)

### Segmentation Faults (RESOLVED)

**Note: All WebSocket segmentation fault issues have been resolved in the current version.**

The previous issues were caused by:
- Incorrect shared_ptr lifecycle management
- Objects inheriting from `std::enable_shared_from_this` created with `make_unique`

These have been fixed by:
- Changing to `std::make_shared` for proper lifecycle management
- Enhanced error handling and timeout protection
- Improved thread safety in WebSocket session handling

### Performance Issues

If you experience slow updates or high latency:

1. **Check system resources:**
   ```bash
   # Monitor CPU/memory usage
   top -p $(pgrep pinnaclemm)
   ```

2. **Reduce update frequency:**
   ```bash
   # Lower visualization update rate
   ./pinnaclemm --mode simulation --enable-visualization --viz-update-interval 2000  # 2 seconds
   ```

3. **Use fewer chart data points:**
   - The dashboard automatically manages chart data history
   - Older data points are automatically pruned

## Dashboard Types

### 1. Main Dashboard (`visualization/static/index.html`)
- Full-featured dashboard with all visualizations
- Chart.js and D3.js charts
- Real-time performance metrics
- Market regime detection
- ML model metrics

### 2. Live Dashboard (`visualization/static/live_dashboard.html`)
- Optimized for live trading
- Coinbase-specific indicators
- "LIVE TRADING" status indicators
- Real-time market data integration

### 3. Test Dashboard (`build/test_dashboard.html`)
- Minimal interface for connection testing
- Real-time connection status
- Message logging
- Connection diagnostics
- Useful for troubleshooting

## Technical Architecture

### WebSocket Implementation
- **Server**: Boost.Beast WebSocket server
- **Protocol**: WebSocket (RFC 6455) over TCP
- **Serialization**: JSON with nlohmann/json
- **Threading**: Asynchronous I/O with Boost.Asio
- **Session Management**: Smart pointer-based lifecycle management

### Data Flow
1. **Data Collection**: PerformanceCollector gathers strategy metrics (1Hz)
2. **Broadcasting**: WebSocketHandler broadcasts to all connected clients
3. **Client Updates**: JavaScript dashboard receives and renders data
4. **Chart Updates**: Chart.js updates visualizations in real-time

### Security Considerations
- WebSocket connections are unencrypted (suitable for localhost)
- No authentication required for visualization data
- API credentials are handled separately by the main application
- Data transmitted includes only performance metrics, not sensitive information

## Port Configuration

Default ports:
- **Simulation**: WebSocket 8080, REST API 8081
- **Live Trading**: WebSocket 8085, REST API 8086
- **Custom Ports**: Use `--viz-ws-port` and `--viz-api-port` flags

Recommended port ranges:
- **Development**: 8080-8099
- **Testing**: 8100-8199
- **Production**: 8200-8299

## Integration with External Tools

### Monitoring with curl
```bash
# Test REST API endpoints
curl http://localhost:8090/api/strategies
curl http://localhost:8090/api/performance/primary_strategy
```

### WebSocket clients
```javascript
// JavaScript WebSocket client
const ws = new WebSocket('ws://localhost:8089');
ws.onopen = () => console.log('Connected');
ws.onmessage = (event) => console.log('Message:', JSON.parse(event.data));
```

## Performance Metrics

Expected performance:
- **WebSocket Latency**: <100ms for data updates
- **Throughput**: 1000+ messages/second
- **Memory Usage**: <50MB for visualization server
- **CPU Usage**: <5% on modern systems

## Support

If you encounter issues not covered in this guide:

1. **Enable debug logging** with `SPDLOG_LEVEL=debug`
2. **Check server logs** for error messages
3. **Use the test dashboard** for connection diagnostics
4. **Verify port availability** and firewall settings
5. **Test with different browsers** if using web dashboard
