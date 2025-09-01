# PinnacleMM Persistence Architecture

## Overview

The persistence subsystem provides durability and crash recovery capabilities to the PinnacleMM trading system. It allows the system to recover its state after unexpected shutdowns or crashes, ensuring that no trading data is lost.

## Architectural Components

### 1. Journal

The Journal records all order book operations in a transaction log, providing a complete history of system activities:

- Uses memory-mapped files for ultra-low latency I/O
- Records order additions, cancellations, and executions
- Uses checksums to ensure data integrity
- Supports compaction to manage file size

### 2. Snapshot Manager

The Snapshot Manager creates periodic point-in-time snapshots of the order book state:

- Stores complete order book state including all active orders
- Enables fast recovery without replaying the entire journal
- Manages snapshot rotation with configurable retention
- Uses atomic file operations to ensure snapshot integrity

### 3. Persistence Manager

The Persistence Manager coordinates the overall persistence operations:

- Maintains journals and snapshots for multiple trading instruments
- Orchestrates the recovery process during system startup
- Performs maintenance such as journal compaction
- Manages the persistence data directory structure

## Data Flow

1. **Write Path**:
   - Order book operations are executed in memory
   - Operations are journaled to the memory-mapped file
   - Periodic snapshots are taken of the complete order book state
   - Journal compaction occurs after successful snapshots

2. **Recovery Path**:
   - Load the most recent snapshot
   - Replay journal entries created after the snapshot
   - Restore the order book to its pre-crash state
   - Resume normal operations

## Memory-Mapped I/O

PinnacleMM uses memory-mapped files for persistence to minimize latency impact:

- Maps files directly into the process address space
- Allows file I/O to occur with simple memory operations
- Leverages the OS page cache for efficient reads and writes
- Eliminates the need for explicit read/write system calls
- Supports both macOS and Linux platforms

## Configuration Parameters

The persistence system is configurable via the following parameters:

- `dataDirectory`: Base directory for journals and snapshots
- `journalSyncIntervalMs`: Interval for forced journal synchronization
- `snapshotIntervalMin`: Interval between snapshots (minutes)
- `keepSnapshots`: Number of snapshots to retain
- `compactionThreshold`: Journal size threshold for compaction

## Deferred Components

### DPDK Integration

DPDK (Data Plane Development Kit) integration was initially planned for Phase 2 but has been deferred due to several constraints:

1. **Hardware Requirements**: DPDK requires specialized network interface cards that support features like hardware offloading and direct memory access, which are not typically available in development environments.

2. **Platform Limitations**: DPDK has limited support for macOS, which is used as a development platform. Its primary target is Linux.

3. **System Modifications**: Implementing DPDK requires kernel module loading, hugepage configuration, and potentially device driver changes, which are risky in shared development environments.

4. **Complexity**: DPDK has a steep learning curve and complicated setup process that would significantly increase development time.

The DPDK integration will be revisited when appropriate hardware and environment are available, likely in a dedicated Linux-based production setting.

## Integration with Live Trading

### Credential Persistence

The persistence system now includes secure credential storage:

- **Encrypted Storage**: API credentials stored with AES-256-CBC + PBKDF2
- **File Location**: `secure_config.json` in the data directory
- **Access Control**: Master password required for decryption
- **Memory Safety**: Credentials cleared from memory after use

### Live Market Data Persistence

With live Coinbase connectivity, the persistence system handles:

- **Real-time Market Updates**: Journals live ticker data from WebSocket feeds
- **Price History**: Maintains historical price and volume data
- **Connection State**: Persists WebSocket connection parameters
- **Recovery**: Resumes live connections after system restarts

### Production Deployment Considerations

- **Data Directory Security**: Ensure proper file permissions (600) for credential files
- **Backup Strategy**: Regular backups of encrypted credential files
- **Monitoring**: Log file access and decryption attempts
- **Rotation**: Periodic credential rotation with automated persistence updates

## Future Enhancements

- Encrypted journals and snapshots for sensitive data
- Distributed persistence for high availability
- Real-time replication to standby instances
- Performance optimizations for specific hardware
- Live order execution persistence for production trading