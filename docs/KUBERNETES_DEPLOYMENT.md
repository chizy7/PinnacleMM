# Kubernetes Deployment Guide

## Overview

PinnacleMM is deployed as a Kubernetes **StatefulSet** with exactly one replica. This is a deliberate design constraint -- a market-making system must never run multiple instances against the same exchange account to avoid self-trading, doubled position risk, and order-ID collisions.

All manifests are in `deploy/k8s/`.

---

## Manifest Summary

| File | Resource | Purpose |
|---|---|---|
| `namespace.yaml` | Namespace | `pinnaclemm` namespace |
| `configmap.yaml` | ConfigMap | `default_config.json` and `ml_config.json` |
| `secret.yaml` | Secret | Template for exchange API credentials |
| `deployment.yaml` | StatefulSet | Application pod with PVC, probes, resource limits |
| `service.yaml` | Services | WebSocket (8080) and REST API (8081) |
| `networkpolicy.yaml` | NetworkPolicy | Ingress/egress rules |
| `pdb.yaml` | PodDisruptionBudget | Prevents eviction of the sole replica |

---

## Prerequisites

- Kubernetes 1.24+
- A StorageClass that supports `ReadWriteOnce` PVCs (e.g., `gp3` on AWS, `standard` on GKE)
- `kubectl` configured for your cluster
- Container image built and pushed to a registry

### Building the Image

```bash
# Build locally
docker build -t pinnaclemm:latest .

# Tag and push to your registry
docker tag pinnaclemm:latest your-registry.com/pinnaclemm:latest
docker push your-registry.com/pinnaclemm:latest
```

---

## Deployment Steps

### 1. Create the Namespace

```bash
kubectl apply -f deploy/k8s/namespace.yaml
```

### 2. Configure Secrets

Edit `deploy/k8s/secret.yaml` with your base64-encoded exchange credentials:

```bash
# Encode your credentials
echo -n 'your-api-key' | base64
echo -n 'your-api-secret' | base64
echo -n 'your-passphrase' | base64

# Apply the secret
kubectl apply -f deploy/k8s/secret.yaml
```

### 3. Apply Configuration

Review `deploy/k8s/configmap.yaml` and adjust the `default_config.json` values for your deployment (risk limits, trading parameters, etc.).

```bash
kubectl apply -f deploy/k8s/configmap.yaml
```

### 4. Deploy the Application

```bash
kubectl apply -f deploy/k8s/deployment.yaml
kubectl apply -f deploy/k8s/service.yaml
kubectl apply -f deploy/k8s/networkpolicy.yaml
kubectl apply -f deploy/k8s/pdb.yaml
```

### 5. Verify

```bash
# Check pod status
kubectl get pods -n pinnaclemm

# Check logs
kubectl logs -n pinnaclemm pinnaclemm-0 -f

# Verify health
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/health

# Verify readiness
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/ready
```

---

## Resource Configuration

The StatefulSet is configured with the following defaults in `deployment.yaml`:

```yaml
resources:
  requests:
    cpu: "500m"
    memory: "512Mi"
  limits:
    cpu: "2"
    memory: "2Gi"
```

Adjust based on your workload. The VaR Monte Carlo simulation is CPU-intensive and benefits from higher CPU limits.

### Persistent Storage

A 10Gi PVC is provisioned for each pod:

```yaml
volumeClaimTemplates:
  - metadata:
      name: pinnaclemm-data
    spec:
      accessModes: ["ReadWriteOnce"]
      resources:
        requests:
          storage: 10Gi
```

This stores journals, snapshots, risk state files, and backups. Size according to your retention needs.

---

## Health Probes

| Probe | Endpoint | Configuration |
|---|---|---|
| **Liveness** | `GET /api/health:8081` | `initialDelaySeconds: 30`, `periodSeconds: 10` |
| **Readiness** | `GET /api/ready:8081` | `initialDelaySeconds: 10`, `periodSeconds: 5` |

The readiness probe returns 200 only when:
- The system has recovered state from persistence
- The circuit breaker is not in OPEN state
- Trading is not halted

---

## Network Policy

The `networkpolicy.yaml` restricts traffic to:

**Ingress**: Only from pods with label `access: pinnaclemm` on ports 8080 (WebSocket) and 8081 (API).

**Egress**: DNS resolution and outbound HTTPS (port 443) to exchange endpoints.

Adjust the egress rules if your exchange uses non-standard ports.

---

## Pod Disruption Budget

The PDB (`pdb.yaml`) sets `minAvailable: 1`, which prevents Kubernetes from voluntarily evicting the sole replica during node drains or cluster upgrades. The pod will only be evicted if the node becomes NotReady.

---

## Scaling

**Do not scale the StatefulSet above 1 replica** unless the application has been modified to support leader election. Running multiple instances against the same exchange account will cause:

- Self-trading (your buy orders fill against your own sell orders)
- Doubled position risk
- Order ID collisions
- Inconsistent state between instances

---

## Updating Configuration

To update risk limits or trading parameters without rebuilding:

```bash
# Edit the ConfigMap
kubectl edit configmap -n pinnaclemm pinnaclemm-config

# Restart the pod to pick up changes
kubectl delete pod -n pinnaclemm pinnaclemm-0
```

The StatefulSet will recreate the pod and crash recovery will restore the last known state.

---

## Monitoring

### Accessing the Dashboard

Port-forward the WebSocket and API services:

```bash
kubectl port-forward -n pinnaclemm svc/pinnaclemm-ws 8080:8080 &
kubectl port-forward -n pinnaclemm svc/pinnaclemm-api 8081:8081 &
```

Then open `visualization/static/index.html` in your browser.

### Key Endpoints

```bash
# Risk state
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/risk/state

# VaR
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/risk/var

# Circuit breaker
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/risk/circuit-breaker

# Alerts
kubectl exec -n pinnaclemm pinnaclemm-0 -- curl -s http://localhost:8081/api/risk/alerts
```

### Log Patterns to Watch

```
# Risk events
"REJECTED_"           # Order rejections
"CLOSED -> OPEN"      # Circuit breaker trips
"Manual trip"         # Manual circuit breaker trips
"EMERGENCY SAVE"      # Emergency state saves
"halt"                # Trading halts
```

---

## Disaster Recovery

See [DISASTER_RECOVERY.md](DISASTER_RECOVERY.md) for detailed procedures covering:

- Crash recovery (automatic)
- Exchange disconnect recovery
- Corrupt journal recovery
- Position reconciliation
- Backup and restore procedures
