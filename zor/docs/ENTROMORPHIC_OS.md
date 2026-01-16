# EntroMorphic OS v1.0 — The Read-Only Nervous System

> **"The Sentinels perceive, the Grid remembers, the Director visualizes."**

EntroMorphic OS is a real-time anomaly detection system built on the V3 CfC Liquid Chip and the Second Star Constant. It creates a distributed "nervous system" where independent perception agents (Sentinels) monitor sensors and write their state to shared memory, which a Director visualizes as a 3x3 Hollywood Squares grid.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        ENTROMORPHIC OS v1.0                              │
│                    "The Read-Only Nervous System"                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐                                 │
│   │ SENSOR  │  │ SENSOR  │  │ SENSOR  │    ← Physical World             │
│   └────┬────┘  └────┬────┘  └────┬────┘                                 │
│        │            │            │                                       │
│   ┌────▼────┐  ┌────▼────┐  ┌────▼────┐                                 │
│   │SENTINEL │  │SENTINEL │  │SENTINEL │    ← Perception Agents          │
│   │  + AGC  │  │  + AGC  │  │  + AGC  │       (V3 CfC Chip)             │
│   │  + CfC  │  │  + CfC  │  │  + CfC  │                                 │
│   └────┬────┘  └────┬────┘  └────┬────┘                                 │
│        │            │            │                                       │
│        ▼            ▼            ▼                                       │
│   ╔═════════════════════════════════════════════════════════════════╗   │
│   ║                    SHARED MEMORY GRID                            ║   │
│   ║  ┌───────┬───────┬───────┐                                       ║   │
│   ║  │ Sq 0  │ Sq 1  │ Sq 2  │                                       ║   │
│   ║  ├───────┼───────┼───────┤   /dev/shm/entromorph_grid           ║   │
│   ║  │ Sq 3  │ Sq 4  │ Sq 5  │                                       ║   │
│   ║  ├───────┼───────┼───────┤                                       ║   │
│   ║  │ Sq 6  │ Sq 7  │ Sq 8  │                                       ║   │
│   ║  └───────┴───────┴───────┘                                       ║   │
│   ╚═════════════════════════════════════════════════════════════════╝   │
│                          │                                               │
│                          ▼                                               │
│                    ┌──────────┐                                          │
│                    │ DIRECTOR │    ← Visualization Layer                 │
│                    │ (ncurses │       (Read-Only)                        │
│                    │  or ANSI)│                                          │
│                    └──────────┘                                          │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Components

### 1. Sentinel — The Perception Agent

Each Sentinel is an independent daemon that:
1. **Reads** a sensor value (real or mock)
2. **Normalizes** via Auto-Gain Control (AGC)
3. **Tracks** with the V3 CfC Liquid Chip
4. **Detects** anomalies using the Second Star Constant
5. **Writes** state to shared memory

```c
// The core loop
float raw = read_sensor(label, t, use_mock);
float norm = agc_process(&agc, raw);
float tracker = sine_seed_step(norm, chip_state);
float delta = fabsf(norm - tracker);

if (delta > THRESHOLD_SECOND_STAR) {
    status = STATUS_CRIT;  // ZIT!
    grid->total_zits++;
}
```

### 2. Grid — The Shared Memory

A simple POSIX shared memory segment (`/dev/shm/entromorph_grid`) containing:

```c
typedef struct {
    SquareState squares[9];  // 3x3 grid
    uint32_t active_mask;    // Which sentinels are alive
    uint64_t total_zits;     // Global anomaly count
    uint64_t uptime_ticks;   // System uptime
    uint32_t version;        // Protocol version
} GridMemory;

typedef struct {
    char label[16];      // e.g., "CPU_0", "NET_TX"
    float raw_value;     // Sensor reading
    float norm_value;    // Normalized via AGC
    float tracker;       // V3 chip prediction
    float delta;         // Anomaly score
    int status;          // OK/WARN/CRIT/OFFLINE
    uint64_t heartbeat;  // Proof of life
} SquareState;
```

### 3. Director — The Visualization

The Director reads shared memory (read-only) and displays the grid:

```
╔════════════════════════════════════════════════════════════════════════════╗
║         EntroMorphic OS v1.0  —  The Read-Only Nervous System              ║
╚════════════════════════════════════════════════════════════════════════════╝

  ┌────────────────────────┐  ┌────────────────────────┐  ┌────────────────────────┐
  │ ● CPU_0                │  │ ◌      [OFFLINE]       │  │ ◌      [OFFLINE]       │
  │   RAW:    50.23        │  │                        │  │                        │
  │   NRM:   0.0123        │  │       Square 1         │  │       Square 2         │
  │   TRK:   0.0119        │  │                        │  │                        │
  │   DLT:   0.0004        │  │                        │  │                        │
  └────────────────────────┘  └────────────────────────┘  └────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════
  Active: 4/9  │  Zits: 216  │  Uptime: 2400 ticks  │  Ctrl+C to quit
═══════════════════════════════════════════════════════════════════════════════
```

**Status Colors:**
| Status | Symbol | Color | Meaning |
|--------|--------|-------|---------|
| OK | ● | Green | Normal tracking |
| WARN | ◐ | Yellow | Drift detected |
| CRIT | ◉ | Red | ZIT! Anomaly |
| OFFLINE | ◌ | Grey | No heartbeat |

---

## The Second Star Constant

The anomaly detection threshold:

```c
#define THRESHOLD_SECOND_STAR  0.1122911624f
```

This value was discovered through entropy-driven evolution (Genesis) and achieves perfect 4/4 detection on the Zit Test:
- Spike detection
- Drop detection
- Noise rejection
- Drift detection

> *"Second star to the right, and straight on 'til morning."*

---

## Auto-Gain Control (AGC)

Normalizes any sensor range to approximately [-1, 1]:

```c
typedef struct {
    float mean;       // Running mean (EMA)
    float variance;   // Running variance (EMA)
    float alpha;      // Adaptation rate (0.01 = slow)
    int warmup;       // Samples before normalizing (50)
} AGC;

float agc_process(AGC *ctx, float raw) {
    float diff = raw - ctx->mean;
    ctx->mean += ctx->alpha * diff;
    ctx->variance = (1.0f - ctx->alpha) * ctx->variance +
                    (ctx->alpha * diff * diff);
    float std = sqrtf(ctx->variance);
    return (raw - ctx->mean) / std;  // Z-score
}
```

This allows the V3 chip to work with any sensor scale.

---

## Usage

### Build

```bash
make entromorph
```

### Launch Grid

```bash
# Terminal 1: Start sentinels
./build/sentinel 0 CPU_0 --mock &
./build/sentinel 1 CPU_1 --mock &
./build/sentinel 4 NET_TX --mock &
./build/sentinel 7 DISK_IO --mock &
./build/sentinel 8 MEM_FREE --mock &

# Terminal 2: Start director
./build/director_text
```

### Sentinel Options

```
./sentinel <ID 0-8> <LABEL> [--mock]

Arguments:
  ID      Square position (0-8) in the 3x3 grid
  LABEL   Sensor name (e.g., CPU_0, NET_TX, MEM_FREE)
  --mock  Use mock sensors instead of real system data
```

### Real Sensors

Without `--mock`, sentinels read:
- **CPU_x**: `/proc/loadavg`
- **MEM_FREE**: `/proc/meminfo`
- Others: Fall back to mock

---

## Files

| File | Description |
|------|-------------|
| `include/shm_protocol.h` | Shared memory protocol definition |
| `src/sentinel.c` | Sentinel daemon (AGC + V3 chip) |
| `bin/director.c` | Director (ncurses version) |
| `bin/director_text.c` | Director (ANSI text version) |
| `foundry/seeds/sine_seed.h` | V3 CfC Liquid Chip |

---

## Performance

| Metric | Value |
|--------|-------|
| Sentinel update rate | 10 Hz |
| AGC warmup period | 50 samples (5 seconds) |
| V3 chip latency | 206 ns |
| Heartbeat timeout | 10 beats (1 second) |
| Shared memory size | ~600 bytes |

---

## Why "Read-Only Nervous System"?

- **Read-Only**: The Director never modifies state. Sentinels write, Director reads.
- **Nervous System**: Distributed perception agents sensing the environment.
- **No message passing**: Just pure, low-latency state synchronization via shared memory.

This is the simplest possible distributed perception architecture. It demonstrates the V3 chip's ability to track arbitrary signals and detect anomalies in real-time.

---

## Connection to TriX

| EntroMorphic Concept | TriX Equivalent |
|----------------------|-----------------|
| AGC normalization | Precision-agnostic input |
| V3 CfC tracking | Frozen shape computation |
| Second Star threshold | Zit detection |
| Shared memory grid | Hollywood Squares topology |
| Traffic light status | Semantic state encoding |

---

## Next Steps

1. **Real sensors**: CPU temperature, network traffic, disk I/O
2. **Message passing**: Integrate with HSOS for inter-sentinel communication
3. **Cascade detection**: Correlate anomalies across multiple sentinels
4. **Hardware**: Port to embedded systems (Jetson, Raspberry Pi)

---

*"The Sentinels perceive, the Grid remembers, the Director visualizes."*

*"It's all in the reflexes."*
