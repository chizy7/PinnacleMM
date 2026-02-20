# PinnacleMM Disaster Recovery Runbook

## 1. Overview

PinnacleMM is a stateful, singleton market-making system deployed as a Kubernetes StatefulSet with exactly one replica. Its disaster recovery (DR) capabilities are built around three pillars:

- **Write-ahead journal** -- every state mutation is appended to a durable journal before being applied. On restart the journal is replayed to reconstruct the last known state.
- **Periodic snapshots** -- the `PersistenceManager` writes full-state snapshots at a configurable interval (default 15 min) and retains the last N snapshots (default 5). Snapshots allow fast recovery without replaying the entire journal.
- **Atomic risk-state files** -- the `DisasterRecovery` module writes risk and strategy state to disk via atomic rename, so a crash can never leave a half-written state file.

All persistent data lives on a 10 Gi PVC mounted at `/data/pinnaclemm`. Logs are written to `/var/log/pinnaclemm`.

---

## 2. Crash Recovery (Automatic)

PinnacleMM recovers automatically when the pod restarts after a crash. No operator action is needed unless recovery fails.

**What happens on startup:**

1. `PersistenceManager::recoverState()` is called.
2. It loads the most recent valid snapshot from `/data/pinnaclemm/<symbol>/snapshots/`.
3. It replays all journal entries written after the snapshot's sequence number from `/data/pinnaclemm/<symbol>/journal/`.
4. Recovered order books are made available via `getRecoveredOrderBooks()`.
5. `DisasterRecovery` restores risk state and strategy state from their atomic files.
6. The strategy resumes quoting.

**Recovery status codes returned by `recoverState()`:**

| Status | Meaning |
|---|---|
| `Success` | State fully recovered from snapshot + journal |
| `CleanStart` | No prior data found; system starts fresh |
| `Failed` | Recovery could not complete; see logs |

**If recovery fails:**

1. Check pod logs: `kubectl logs -n pinnaclemm pinnaclemm-0`.
2. Look for `PersistenceManager` or `DisasterRecovery` error messages.
3. If the journal is corrupt, follow Section 4 (Corrupt Journal Recovery).
4. If the PVC is missing or inaccessible, verify the StorageClass and PVC status: `kubectl get pvc -n pinnaclemm`.

---

## 3. Exchange Disconnect Recovery

When the exchange WebSocket or FIX connection drops, PinnacleMM performs automatic reconnection.

**Automatic behavior:**

1. The exchange connector detects the disconnect.
2. Open orders are assumed stale and are cancelled on reconnection.
3. The connector retries with exponential backoff.
4. On reconnection, a full position reconciliation is triggered: the system queries the exchange for current positions and open orders.
5. Any discrepancies between local state and exchange state are logged and corrected.

**Manual intervention (if auto-reconnect fails after extended outage):**

1. Verify network connectivity from the pod:
   ```bash
   kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s https://api.exchange.com/status
   ```
2. Check exchange status pages for planned maintenance.
3. If the exchange is up but the connector cannot reconnect, restart the pod:
   ```bash
   kubectl delete pod -n pinnaclemm pinnaclemm-0
   ```
   The StatefulSet will recreate the pod and crash recovery will restore state.

---

## 4. Corrupt Journal Recovery

If the journal file is truncated or corrupted (e.g., due to storage failure), PinnacleMM falls back to the most recent valid snapshot.

**Symptoms:**
- Logs contain `Journal replay failed` or `CRC mismatch` errors.
- `recoverState()` returns `Failed`.

**Procedure:**

1. Stop the pod to prevent further writes:
   ```bash
   kubectl scale statefulset -n pinnaclemm pinnaclemm --replicas=0
   ```
2. Attach to the PVC from a debug pod:
   ```bash
   kubectl run -n pinnaclemm debug --rm -it --image=busybox \
     --overrides='{"spec":{"containers":[{"name":"debug","volumeMounts":[{"name":"pinnaclemm-data","mountPath":"/data/pinnaclemm"}]}],"volumes":[{"name":"pinnaclemm-data","persistentVolumeClaim":{"claimName":"pinnaclemm-data-pinnaclemm-0"}}]}}'
   ```
3. Back up the corrupt journal:
   ```bash
   cp -r /data/pinnaclemm/<symbol>/journal /data/pinnaclemm/<symbol>/journal.corrupt.bak
   ```
4. Delete the corrupt journal files:
   ```bash
   rm /data/pinnaclemm/<symbol>/journal/*.journal
   ```
5. Verify that snapshots are intact:
   ```bash
   ls -la /data/pinnaclemm/<symbol>/snapshots/
   ```
6. Exit the debug pod and scale back up:
   ```bash
   kubectl scale statefulset -n pinnaclemm pinnaclemm --replicas=1
   ```
7. On startup, `recoverState()` will load from the latest snapshot with no journal to replay. The system resumes from the snapshot point. Any state changes between the last snapshot and the crash are lost.

---

## 5. Manual Position Adjustment

If the system's internal position tracking drifts from the exchange's actual positions (e.g., after a partial recovery or manual exchange-side trade), you may need to reconcile manually.

**Procedure:**

1. Query current positions from the exchange via the REST API:
   ```bash
   kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/positions
   ```
2. Compare with exchange-reported positions (check the exchange dashboard or API directly).
3. If there is a discrepancy, trigger a forced reconciliation:
   ```bash
   kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -X POST http://localhost:8081/api/reconcile
   ```
4. Verify the updated positions:
   ```bash
   kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/positions
   ```
5. Check logs for reconciliation results:
   ```bash
   kubectl logs -n pinnaclemm pinnaclemm-0 --tail=50
   ```

---

## 6. Backup and Restore Procedures

### 6.1 Create a Backup

Create an on-demand backup of the entire data directory:

```bash
# Create a timestamped backup tarball from the running pod
kubectl exec -n pinnaclemm pinnaclemm-0 -- \
  tar czf /tmp/pinnaclemm-backup-$(date +%Y%m%d-%H%M%S).tar.gz \
  -C /data/pinnaclemm .

# Copy the backup to your local machine
kubectl cp pinnaclemm/pinnaclemm-0:/tmp/pinnaclemm-backup-*.tar.gz ./backups/
```

### 6.2 List Available Backups

List snapshots available on the PVC:

```bash
kubectl exec -n pinnaclemm pinnaclemm-0 -- \
  find /data/pinnaclemm -name '*.snapshot' -type f | sort
```

List local backup tarballs:

```bash
ls -lht ./backups/pinnaclemm-backup-*.tar.gz
```

### 6.3 Restore from Backup

1. Scale down the StatefulSet:
   ```bash
   kubectl scale statefulset -n pinnaclemm pinnaclemm --replicas=0
   ```
2. Copy the backup tarball into a debug pod attached to the PVC:
   ```bash
   kubectl run -n pinnaclemm restore --rm -it --image=busybox \
     --overrides='{"spec":{"containers":[{"name":"restore","volumeMounts":[{"name":"pinnaclemm-data","mountPath":"/data/pinnaclemm"}]}],"volumes":[{"name":"pinnaclemm-data","persistentVolumeClaim":{"claimName":"pinnaclemm-data-pinnaclemm-0"}}]}}'
   ```
3. From another terminal, copy the backup in:
   ```bash
   kubectl cp ./backups/pinnaclemm-backup-YYYYMMDD-HHMMSS.tar.gz \
     pinnaclemm/restore:/tmp/backup.tar.gz
   ```
4. In the debug pod, clear old data and extract:
   ```bash
   rm -rf /data/pinnaclemm/*
   tar xzf /tmp/backup.tar.gz -C /data/pinnaclemm
   ```
5. Exit the debug pod and scale back up:
   ```bash
   kubectl scale statefulset -n pinnaclemm pinnaclemm --replicas=1
   ```
6. Verify recovery in logs:
   ```bash
   kubectl logs -n pinnaclemm pinnaclemm-0 -f
   ```

---

## 7. Split-Brain Prevention

PinnacleMM is deployed as a StatefulSet with exactly **one replica**. This is a deliberate design choice:

- A market-making system must not have two instances simultaneously placing orders on the same exchange account. Duplicate quoting would cause self-trading, doubled position risk, and order-ID collisions.
- The `PodDisruptionBudget` (minAvailable: 1) prevents Kubernetes from evicting the sole replica during voluntary disruptions (node drains, cluster upgrades).
- The StatefulSet guarantees at-most-one semantics: Kubernetes will not start a replacement pod until the old pod's volume is fully released.

**Never scale the StatefulSet above 1 replica** unless the application has been explicitly modified to support leader election.

---

## 8. Risk State Recovery

Risk state is automatically restored on startup by the `DisasterRecovery` module.

**What is recovered:**

- Current P&L, drawdown tracking, and daily loss counters.
- Position limits and exposure tracking.
- VaR model state (return history and current VaR estimate).
- Circuit breaker state (trip counts, cooldown timers).
- Alert history.
- Strategy-specific state (inventory, last quote prices, ML model parameters).

**How it works:**

1. `DisasterRecovery::saveRiskState()` writes risk and strategy state to atomic files on every state change (write to temp file, then `rename()`).
2. On startup, `DisasterRecovery` reads these files and restores the full risk context.
3. If the risk state files are missing or corrupt, the system starts with default risk limits from the configuration and logs a warning.

**Manual verification after recovery:**

```bash
kubectl exec -n pinnaclemm pinnaclemm-0 -- \
  curl -s http://localhost:8081/api/risk/status | python3 -m json.tool
```

---

## 9. Circuit Breaker Recovery

The circuit breaker protects the system from runaway losses during extreme market conditions.

**Circuit breaker triggers (from configuration):**

| Trigger | Threshold |
|---|---|
| 1-minute price move | 2.0% |
| 5-minute price move | 5.0% |
| Spread widening | 3x normal |
| Volume spike | 5x normal |
| Execution latency | > 10 ms |

**Recovery behavior:**

1. When the circuit breaker trips, all quoting stops immediately and open orders are cancelled.
2. After the cooldown period (default 30 seconds), the circuit breaker enters a **half-open** state.
3. During the half-open test window (default 10 seconds), the system places limited test quotes.
4. If the test period completes without re-triggering, the circuit breaker fully resets and normal quoting resumes.
5. If a trigger fires again during the half-open window, the circuit breaker re-trips and the cooldown restarts.

**Manual override (use with caution):**

```bash
# Force-reset the circuit breaker
kubectl exec -n pinnaclemm pinnaclemm-0 -- \
  curl -X POST http://localhost:8081/api/risk/circuit-breaker/reset
```

After a pod restart, the circuit breaker state is restored from the risk state file. If the system was mid-cooldown at the time of the crash, the cooldown timer restarts from the beginning.

---

## 10. Monitoring and Alerting

### 10.1 Health Endpoints

| Endpoint | Port | Purpose |
|---|---|---|
| `GET /api/health` | 8081 | Liveness check. Returns 200 if the process is alive. Used by Kubernetes liveness probe. |
| `GET /api/ready` | 8081 | Readiness check. Returns 200 when the system has recovered state, connected to the exchange, and is ready to accept traffic. |

### 10.2 Prometheus Metrics (if enabled)

Expose the API service to your monitoring stack:

```yaml
# ServiceMonitor example (for Prometheus Operator)
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: pinnaclemm-monitor
  namespace: pinnaclemm
spec:
  selector:
    matchLabels:
      app: pinnaclemm
      component: api
  endpoints:
    - port: api
      path: /api/metrics
      interval: 15s
```

### 10.3 Recommended Alerts

Configure the following alerts in your alert manager:

| Alert | Condition | Severity |
|---|---|---|
| PinnacleMM Down | `up{job="pinnaclemm"} == 0` for 2 min | Critical |
| High Drawdown | drawdown > warning threshold (80% of limit) | Warning |
| Circuit Breaker Tripped | circuit breaker state != closed | Warning |
| Position Limit Near | position > 80% of max | Warning |
| Journal Write Latency | journal sync > 500 ms | Warning |
| Recovery Failed | recovery status == Failed on startup | Critical |
| Exchange Disconnected | exchange connection state != connected for 60s | Critical |
| Daily Loss Limit Approaching | daily P&L loss > 80% of daily limit | Critical |

### 10.4 Log Monitoring

Aggregate logs with your cluster logging stack (e.g., Fluentd, Loki). Key log patterns to watch:

```
# Circuit breaker events
"circuit.*breaker.*tripped"
"circuit.*breaker.*reset"

# Recovery events
"recoverState.*Success"
"recoverState.*Failed"
"Journal replay failed"

# Risk events
"daily.*loss.*limit"
"max.*drawdown.*exceeded"
"position.*limit.*breach"

# Exchange connectivity
"exchange.*disconnect"
"exchange.*reconnect"
```

---

## Quick Reference Card

| Scenario | Action | Automatic? |
|---|---|---|
| Pod crash | Journal replay + risk state restore | Yes |
| Exchange disconnect | Reconnect with backoff + reconcile | Yes |
| Corrupt journal | Delete journal, restart (snapshot fallback) | Manual |
| Position drift | `POST /api/reconcile` | Manual |
| Circuit breaker trip | Wait for cooldown + half-open test | Yes |
| Full data loss | Restore from backup tarball | Manual |
| Node drain | PDB prevents eviction of sole replica | Yes |
| Risk state corrupt | Starts with default config limits | Yes (degraded) |
