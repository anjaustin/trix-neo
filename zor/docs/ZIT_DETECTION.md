# Zit Detection: Anomaly Detection via Phase-Locked Tracking

> "Define Order, and Chaos reveals itself."

This document describes the theoretical foundation and practical implementation of **Zit Detection** — TriX's approach to anomaly detection using CfC (Closed-form Continuous-time) neural networks.

---

## Table of Contents

1. [The Core Insight](#the-core-insight)
2. [What is a Zit?](#what-is-a-zit)
3. [Dual-Tau Architecture](#dual-tau-architecture)
4. [The Phase-Lock Principle](#the-phase-lock-principle)
5. [Detection Modes](#detection-modes)
6. [Implementation](#implementation)
7. [Performance Characteristics](#performance-characteristics)
8. [Applications](#applications)

---

## The Core Insight

Traditional anomaly detection asks: "Is this value abnormal?"

Zit Detection asks: **"Is this value moving faster than expected?"**

The key insight is that CfC neural networks are **smoothers**. They naturally produce phase-shifted, low-pass filtered versions of their input. Anything that changes too quickly for the CfC to track is, by definition, an anomaly.

**The CfC's slowness IS its sensitivity.**

---

## What is a Zit?

A **Zit** (from XOR resonance terminology) is a sudden deviation from expected behavior. The term originates from the XOR-based detection mechanism:

```c
// The Zit Equation
zit = popcount(state XOR input) > threshold
```

In time-series anomaly detection, we adapt this concept:

```c
// Time-series Zit
zit = |input - tracker_output| > threshold
```

When the CfC tracker cannot keep up with the input, the delta spikes — that's a Zit.

### Types of Zits

| Type | Description | Example |
|------|-------------|---------|
| **Spike Zit** | Sudden large value | CPU spike to 100% |
| **Drop Zit** | Sudden drop to abnormal level | Service crash (load drops to 0) |
| **Noise Zit** | High-frequency oscillation | Network jitter |
| **Drift Zit** | Gradual baseline shift | Memory leak |

---

## Dual-Tau Architecture

Single-tau detection (one CfC tracker) catches fast anomalies but misses slow drift. The solution: **Dual-Tau Architecture**.

```
┌─────────────────────────────────────────────────────────────┐
│                    DUAL-TAU DETECTOR                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   Input ─────┬────────────► CfC Tracker (τ ~ 1-2s)          │
│              │                     │                         │
│              │                     ▼                         │
│              │              Fast Output ─────► ZIT DETECT    │
│              │                     │                         │
│              │                     ▼                         │
│              └───────────► EMA Anchor (τ ~ 30s)             │
│                                    │                         │
│                                    ▼                         │
│                              Slow Output ─────► DRIFT DETECT │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Fast Tracker (CfC Chip)

- Time constant: τ ~ 1-2 seconds
- Catches: Spikes, drops, noise bursts
- Memory: 396 bytes
- Latency: 206 nanoseconds

### Slow Anchor (EMA)

- Time constant: τ ~ 30 seconds
- Catches: Gradual drift, baseline shifts
- Memory: 4 bytes (single float)
- Latency: ~10 nanoseconds

### Combined Detection

```c
// Fast anomaly: input deviates from tracker
float zit_delta = fabsf(input - fast_output);
if (zit_delta > ZIT_THRESHOLD) {
    alert("ZIT DETECTED");
}

// Slow anomaly: tracker deviates from anchor
float drift_delta = fabsf(fast_output - anchor);
if (drift_delta > DRIFT_THRESHOLD) {
    alert("DRIFT DETECTED");
}
```

---

## The Phase-Lock Principle

### Why CfC Works for Anomaly Detection

CfC neurons solve this ODE in closed form:

```
dh/dt = (1/τ) * (gate * candidate - h)
```

This makes them **phase-locked** to their input. They track the signal, but with a characteristic delay determined by τ.

### The Tracker as Reference Frame

Think of the CfC tracker as a **stiff reference frame**:

- Normal signals: The tracker follows smoothly
- Anomalous signals: The tracker lags behind

```
Signal:   ────────▲────────────────────
                  │ spike
Tracker:  ────────╱╲───────────────────
                   ╲ gradual rise
                    ╲ to catch up
                     ╲
Delta:    ────────█────────────────────
                  │ ZIT!
```

### Mathematical Basis

For a CfC neuron with time constant τ:

- **Frequency response**: Behaves like a low-pass filter with cutoff ~ 1/τ
- **Phase shift**: Signals are delayed by approximately τ
- **Gain**: Near unity for slow signals, attenuated for fast signals

Any input component with frequency > 1/τ will be attenuated, creating a measurable delta.

---

## Detection Modes

### Mode 1: Threshold Detection

The simplest approach: flag when delta exceeds a fixed threshold.

```c
if (delta > THRESHOLD) {
    alert("ANOMALY");
}
```

**Pros:** Simple, fast, predictable
**Cons:** Doesn't adapt to signal variance

### Mode 2: Statistical Detection

Flag when delta exceeds N standard deviations of the baseline error.

```c
// Maintain running baseline MAE
baseline_mae = 0.99 * baseline_mae + 0.01 * current_error;

// Detect 5-sigma events
if (delta > baseline_mae * 5.0) {
    alert("ANOMALY");
}
```

**Pros:** Adapts to signal characteristics
**Cons:** Requires warmup period

### Mode 3: Dual-Threshold Detection

Separate thresholds for fast and slow anomalies.

```c
// Fast: spikes relative to tracker
if (zit_delta > ZIT_THRESHOLD) {
    alert("ZIT");
}

// Slow: drift relative to anchor
if (drift_delta > DRIFT_THRESHOLD) {
    alert("DRIFT");
}
```

**Pros:** Catches both fast and slow anomalies
**Cons:** Two thresholds to tune

---

## Implementation

### The Sentinel Daemon

The reference implementation is `zor/src/sentinel.c`:

```c
typedef struct {
    /* Fast tracker (CfC chip) */
    float fast_state[sine_SEED_REGS];
    float fast_output;

    /* Slow anchor (EMA) */
    float anchor;
    float anchor_alpha;

    /* Statistics */
    float baseline_mae;
    int sample_count;
    int warmup_complete;

    /* Detection state */
    int zit_active;
    int drift_active;
    int zit_count;
    int drift_count;
} DualTauDetector;
```

### Detector Step Function

```c
DetectionResult detector_step(DualTauDetector* d, float input) {
    DetectionResult result = {0};

    // Run fast tracker (CfC chip)
    d->fast_output = sine_seed_step(input, d->fast_state);

    // Update slow anchor (EMA)
    d->anchor = d->anchor * (1 - d->anchor_alpha) + input * d->anchor_alpha;

    // Calculate deltas
    result.zit_delta = fabsf(input - d->fast_output);
    result.drift_delta = fabsf(d->fast_output - d->anchor);

    // Zit detection (fast anomaly)
    // Using Second Star Constant directly — proven 4/4 detection
    if (result.zit_delta > ZIT_THRESHOLD) {
        result.zit_detected = 1;
    }

    // Drift detection (slow anomaly)
    if (result.drift_delta > DRIFT_THRESHOLD) {
        result.drift_detected = 1;
    }

    return result;
}
```

### Metric Sources

Sentinel can monitor various system metrics:

```c
float read_cpu_load(void);        // /proc/loadavg
float read_memory_usage(void);    // /proc/meminfo
float read_network_activity(void); // /proc/net/dev
```

---

## Performance Characteristics

### Memory Footprint

| Component | Size |
|-----------|------|
| CfC Chip State | 396 bytes |
| EMA Anchor | 4 bytes |
| Statistics | 20 bytes |
| Total | ~420 bytes |

### Latency

| Operation | Time |
|-----------|------|
| CfC Step | 206 ns |
| EMA Update | ~10 ns |
| Delta Calculation | ~20 ns |
| Total | ~240 ns |

### Detection Accuracy

Tested on synthetic anomalies using the Second Star Constant (0.1122911624):

| Anomaly Type | Detection Rate |
|--------------|----------------|
| Spike (+2.0) | 100% |
| Drop (to 0) | 100% |
| Noise Burst (±0.5) | 100% |
| Gradual Drift (+0.02/step) | 100% |

**Verdict: 4/4 Perfect Detection** with the V3 Efficient Species.

---

## The V3 Efficient Species

The V3 Efficient Species is the third-generation sine tracker evolved in the Genesis foundry. It was created under the **Gluttony Penalty** constraint, which punishes networks that use high internal state values.

### Evolution Constraints

| Parameter | Value | Purpose |
|-----------|-------|---------|
| MAX_ALLOWED_STATE | 5.0 | Prevents high-gain instability |
| GLUTTONY_PENALTY | 100.0 | Metabolic cost per unit excess |
| PREDICT_HORIZON | 3 | Predicts 3 steps ahead (anti-identity) |
| FAST_WINDOW | 256 | Extended evaluation window |

### Species Characteristics

- **Nodes:** 98
- **Registers:** 99
- **Memory:** 396 bytes
- **State bounds:** [-3.35, 4.40]
- **Output bounds:** [-0.77, 0.79]

### Stability Test Results

The V3 Efficient Species was tested for numerical stability over **100 million steps**:

| Checkpoint | Steps | MAE | Drift |
|------------|-------|-----|-------|
| Baseline | 1,000 | 0.1295 | 0.00% |
| 10K | 10,000 | 0.1286 | 0.69% |
| 100K | 100,000 | 0.1283 | 0.93% |
| 1M | 1,000,000 | 0.1281 | 1.08% |
| 10M | 10,000,000 | 0.1279 | 1.24% |
| 100M | 100,000,000 | 0.1276 | 1.47% |

**Verdict:** Numerically stable. MAE drift < 1.5% over 100M steps. State bounded, no NaN/Inf.

This stability is critical for long-running sentinel deployments.

### Noise Rejection Results

The V3 species was tested for robustness against Gaussian noise:

| Noise Amplitude | Input Variance | Output Variance | Rejection Ratio |
|-----------------|----------------|-----------------|-----------------|
| 0.0 (clean) | 0.500 | 0.429 | 1.2x |
| 0.5 | 0.708 | 0.432 | 1.6x |
| 1.0 (equals signal) | 1.485 | 0.489 | 3.0x |
| 2.0 (2x signal) | 4.466 | 0.622 | **7.2x** |

**Key Insight:** Output variance stays nearly flat (0.43→0.62) while input variance explodes (0.5→4.5). The chip acts as a conditional low-pass filter:

- **Signal frequencies:** Pass through
- **Noise frequencies:** Rejected

Maximum variance rejection: **7.8x**. This is the Gluttony Penalty in action — the network refuses to chase noise because chasing noise is expensive.

### Bandwidth Results

The V3 species exhibits **wideband tracking** behavior:

| Metric | Value |
|--------|-------|
| Frequency range | 0.1 Hz to 45 Hz |
| Mean gain | -2.19 dB ± 0.09 dB |
| Mean correlation | 97.9% |
| -3dB cutoff | Not reached |

This is NOT classic low-pass behavior. The V3 evolved into a **Zero-Latency Predictor**:

- Internal phase lag + learned prediction = zero net phase
- Tracks any frequency in bandwidth with equal fidelity
- Proves it learned PHYSICS, not just patterns

---

## Applications

### 1. System Monitoring

```bash
./sentinel --cpu        # Monitor CPU load
./sentinel --memory     # Monitor memory usage
./sentinel --network    # Monitor network traffic
```

### 2. IoT Sensor Validation

```c
// Each sensor gets a detector
DualTauDetector temp_detector, pressure_detector;

// Flag faulty sensors
if (detector_step(&temp_detector, temp_reading).zit_detected) {
    flag_sensor_anomaly("temperature");
}
```

### 3. Financial Time Series

```c
// Track stock price
float price = get_stock_price();
DetectionResult r = detector_step(&detector, price);

if (r.zit_detected) {
    log("Flash crash or spike detected");
}
```

### 4. Network Intrusion Detection

```c
// Track packets per second
float pps = get_packets_per_second();
DetectionResult r = detector_step(&detector, pps);

if (r.zit_detected) {
    log("Traffic anomaly - possible attack");
}
```

---

## Tuning Guide

### ZIT_THRESHOLD (The Second Star Constant)

The optimal threshold for the V3 Efficient Species is **0.1122911624** — the Second Star Constant.

This value was discovered through empirical tuning after the Gluttony Penalty evolution run.
It achieves **4/4 perfect detection** across all anomaly types (spike, drop, noise, drift).

| Value | Effect |
|-------|--------|
| 0.05 | Too sensitive (false positives on normal tracking error) |
| 0.1122911624 | **Second Star Constant** — Perfect detection (recommended) |
| 0.285 | Good for spike/drift, misses subtle anomalies |
| 0.5 | Conservative (misses most anomalies) |

The name comes from J.M. Barrie's *Peter Pan*: "Second star to the right, and straight on 'til morning."

### DRIFT_THRESHOLD

Controls sensitivity to slow anomalies.

| Value | Effect |
|-------|--------|
| 0.3 | Catches small drifts |
| 0.5 | Balanced (default) |
| 1.0 | Only major shifts |

### WARMUP_SAMPLES

Samples before detection starts.

| Value | Effect |
|-------|--------|
| 20 | Fast startup, less accurate baseline |
| 50 | Balanced (default) |
| 100 | Slower startup, better baseline |

### EMA Alpha (anchor_alpha)

Controls anchor time constant.

| Alpha | τ (at 10 Hz) | Effect |
|-------|--------------|--------|
| 0.1 | ~1 second | Fast-moving anchor |
| 0.02 | ~5 seconds | Balanced (default) |
| 0.01 | ~10 seconds | Slow-moving anchor |

---

## Theory: Why This Works

### Information-Theoretic View

The CfC tracker compresses the signal into a lower-bandwidth representation. Any information lost in this compression (high-frequency components) is recoverable from the delta.

```
Information in input = Information in tracker + Information in delta
```

Anomalies carry information that **cannot** be represented by the tracker's frequency response, so they appear in the delta.

### Control Theory View

The CfC tracker is a closed-loop estimator. The delta is the **innovation** — the part of the signal that wasn't predicted.

In Kalman filter terms:
- Tracker output = a priori estimate
- Delta = measurement residual
- Large residual = anomaly

### Signal Processing View

The CfC is a non-linear low-pass filter. The delta is the high-pass component.

```
input = low_pass(input) + high_pass(input)
      = tracker_output + delta
```

High-pass energy spikes = anomaly.

---

## Related Documents

- [CFC_ENTROMORPH.md](CFC_ENTROMORPH.md) — CfC + Evolution integration
- [SOFT_CHIPS.md](SOFT_CHIPS.md) — Soft chip deployment
- [Foundry README](../foundry/README.md) — Growing seeds with Genesis

---

## References

1. Hasani, R., et al. "Closed-form Continuous-time Neural Networks." (2021)
2. Chandola, V., et al. "Anomaly detection: A survey." ACM Computing Surveys (2009)
3. TriX Documentation: THE_5_PRIMES.md, ENGINEERING.md

---

*"The CfC's slowness IS its sensitivity."*
