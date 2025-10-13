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
- Stores and provides access to recovered order books
- Performs maintenance such as journal compaction
- Manages the persistence data directory structure

#### Order Book Recovery API

The Persistence Manager provides a complete API for managing recovered order books:

- `recoverState()` - Performs full recovery (snapshots + journals)
- `getRecoveredOrderBook(symbol)` - Retrieves a specific recovered order book
- `getAllRecoveredOrderBooks()` - Returns all recovered order books as a map
- `hasRecoveredOrderBooks()` - Checks if any order books were recovered
- `clearRecoveredOrderBooks()` - Clears recovered order books after use

All recovered order books are stored in memory after recovery and can be retrieved by the application for immediate use.

## Data Flow

1. **Write Path**:
   - Order book operations are executed in memory
   - Operations are journaled to the memory-mapped file
   - Periodic snapshots are taken of the complete order book state
   - Journal compaction occurs after successful snapshots

2. **Recovery Path**:
   - **Snapshot Recovery**:
     - Enumerate all snapshot directories for each trading symbol
     - Load the most recent valid snapshot for each symbol
     - Restore order book state from snapshot data
     - **Store recovered order books** in PersistenceManager
     - Log recovery progress and validate snapshot integrity
   - **Journal Replay**:
     - Enumerate all journal files from the journals directory
     - For each symbol, replay journal entries after the latest checkpoint
     - Apply operations (add, cancel, execute) to restore complete state
     - Skip entries before the checkpoint to avoid duplicate processing
     - **Update and store recovered order books** with replayed state
   - **Application Integration**:
     - Application calls `getRecoveredOrderBook(symbol)` to retrieve order books
     - Recovered order books contain all persisted orders and state
     - If no recovered order book exists, application creates a new one
     - Application continues with either recovered or new order book
     - Verify recovered order book state consistency
     - Log successful recovery with entry counts and order totals
     - Resume normal operations with full trading state restored

## Thread Safety

The persistence system is fully thread-safe:

- **Mutex Protection**: All shared data structures protected with dedicated mutexes
  - `m_journalsMutex` - Protects journal map
  - `m_snapshotManagersMutex` - Protects snapshot manager map
  - `m_recoveredOrderBooksMutex` - Protects recovered order book map
- **Atomic Operations**: Journal and snapshot operations use atomic file operations
- **Lock Ordering**: Consistent lock ordering prevents deadlocks
- **Concurrent Access**: Multiple threads can safely:
  - Retrieve different order books simultaneously
  - Perform recovery while application continues
  - Execute maintenance without blocking recovery

## Memory-Mapped I/O

PinnacleMM uses memory-mapped files for persistence to minimize latency impact:

- Maps files directly into the process address space
- Allows file I/O to occur with simple memory operations
- Leverages the OS page cache for efficient reads and writes
- Eliminates the need for explicit read/write system calls
- Supports both macOS and Linux platforms

## Maintenance Operations

The persistence system includes comprehensive maintenance capabilities to ensure optimal performance and storage management:

### Journal Compaction
- **Automatic Trigger**: Compacts journals when entry count exceeds threshold (default: 1M entries)
- **Checkpoint-Based**: Removes all entries before the latest snapshot checkpoint
- **Atomic Operations**: Uses temporary files with atomic rename for safe compaction
- **Logging**: Detailed logging of compaction operations and outcomes

### Snapshot Rotation
- **Retention Policy**: Keeps N most recent snapshots per symbol (default: 5)
- **Automatic Cleanup**: Removes old snapshots during maintenance operations
- **Sorted Deletion**: Deletes oldest snapshots first, preserving recent state

### Maintenance Workflow
1. Lock persistence resources to prevent concurrent modifications
2. For each trading symbol:
   - Check if journal exceeds compaction threshold
   - Compact journal to remove pre-checkpoint entries
   - Clean up old snapshots based on retention policy
3. Process snapshot managers without journals
4. Log maintenance summary (journals compacted, snapshots cleaned)

## Usage Example

Here's how to use the persistence system in your application:

```cpp
// 1. Initialize persistence
auto& persistenceManager = PersistenceManager::getInstance();
persistenceManager.initialize("data");

// 2. Attempt to recover previous state
if (persistenceManager.recoverState()) {
    spdlog::info("Successfully recovered persistence state");
} else {
    spdlog::info("No previous state to recover (clean start)");
}

// 3. Try to get a recovered order book
std::shared_ptr<OrderBook> orderBook;
orderBook = persistenceManager.getRecoveredOrderBook("BTC-USD");

if (orderBook) {
    // Use the recovered order book with existing orders
    spdlog::info("Using recovered order book with {} orders",
                 orderBook->getOrderCount());
} else {
    // No recovered data, create a new order book
    orderBook = std::make_shared<OrderBook>("BTC-USD");
    spdlog::info("Created new order book");
}

// 4. Continue normal trading operations
// The order book will automatically journal all operations

// 5. Periodic maintenance (e.g., in a maintenance thread)
persistenceManager.performMaintenance();

// 6. On shutdown
persistenceManager.shutdown();
```

## Configuration Parameters

The persistence system is configurable via the following parameters:

- `dataDirectory`: Base directory for journals and snapshots
- `journalSyncIntervalMs`: Interval for forced journal synchronization
- `snapshotIntervalMin`: Interval between snapshots (minutes)
- `keepSnapshots`: Number of snapshots to retain (default: 5)
- `compactionThreshold`: Journal size threshold for compaction (default: 1,000,000 entries)

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

The persistence system now includes enhanced secure credential storage:

- **Enhanced Encryption**: API credentials stored with AES-256-CBC + unique salt generation + 100,000 PBKDF2 iterations
- **File Location**: `secure_config.json` in the data directory
- **Access Control**: Master password required for decryption with secure input masking
- **Input Validation**: All credential inputs validated before encryption to prevent injection attacks
- **Memory Safety**: Credentials cleared from memory using volatile operations after use
- **Audit Trail**: All credential access attempts logged for security monitoring
- **Integrity Protection**: File tampering detection through checksum validation

### Live Market Data Persistence

With live Coinbase connectivity, the persistence system handles:

- **Real-time Market Updates**: Journals live ticker data from WebSocket feeds
- **Price History**: Maintains historical price and volume data
- **Connection State**: Persists WebSocket connection parameters
- **Recovery**: Resumes live connections after system restarts

### Production Deployment Considerations

- **Data Directory Security**: Ensure proper file permissions (600) for credential files
- **Backup Strategy**: Regular backups of encrypted credential files with salt preservation
- **Monitoring**: Comprehensive audit logging of all file access and decryption attempts
- **Rotation**: Periodic credential rotation with automated persistence updates and new salt generation
- **Rate Limiting**: API access rate limiting to prevent brute force attacks
- **Certificate Validation**: SSL certificate pinning for all WebSocket connections
- **Input Sanitization**: All user inputs validated and sanitized before persistence
- **Memory Protection**: Sensitive data cleared from memory using secure erasure techniques

## Future Enhancements

- **Enhanced Encryption**: Encrypted journals and snapshots for sensitive data
- **Distributed Security**: Distributed persistence for high availability with synchronized security policies
- **Real-time Replication**: Secure replication to standby instances with encrypted channels
- **Hardware Security**: Integration with hardware security modules (HSMs) for key storage
- **Performance Optimizations**: Security-aware optimizations for specific hardware
- **Live Order Execution**: Secure persistence for production trading with audit trails
- **Zero-Trust Architecture**: Complete zero-trust security model for all persistence operations
- **Quantum-Resistant Cryptography**: Future-proofing against quantum computing threats
