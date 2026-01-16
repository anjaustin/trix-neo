# The Fuse Box — Universal Chip Tester

> **"Does your chip detect anomalies?"**

The Fuse Box is the validation harness for forged chips. It tests any chip against raw data and reports anomaly detection statistics.

---

## Quick Start

```bash
# Build fuse tester for your chip
make fuse-compile CHIP_FILE=my_chip.h CHIP_NAME=MY_CHIP

# Test on data
./build/fuse_test data.csv

# Test with custom threshold
./build/fuse_test data.csv 1.5

# Quiet mode (summary only)
./build/fuse_test data.csv 1.5 --quiet
```

---

## How It Works

```
┌─────────────────────────────────────────────────────────────────┐
│                       THE FUSE BOX                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   CSV Data          Forged Chip                                  │
│   ┌──────────┐      ┌──────────┐                                │
│   │ 45.2     │      │ my_chip.h│                                │
│   │ 47.1     │      │          │                                │
│   │ 43.8     │      │ AGC+CfC  │                                │
│   └──────────┘      └────┬─────┘                                │
│        │                 │                                       │
│        │                 │                                       │
│        ▼                 ▼                                       │
│   ┌─────────────────────────────────────┐                       │
│   │         FUSE BOX HARNESS            │                       │
│   │                                     │                       │
│   │  for each sample:                   │                       │
│   │    1. Read raw value                │                       │
│   │    2. Execute chip (AGC automatic)  │                       │
│   │    3. Compute delta                 │                       │
│   │    4. Compare to threshold          │                       │
│   │    5. Record statistics             │                       │
│   │                                     │                       │
│   └─────────────────────────────────────┘                       │
│                      │                                           │
│                      ▼                                           │
│   ┌─────────────────────────────────────┐                       │
│   │           VERDICT                   │                       │
│   │                                     │                       │
│   │  - Zit count and percentage         │                       │
│   │  - Delta statistics                 │                       │
│   │  - Correlation score                │                       │
│   │  - Pass/Fail determination          │                       │
│   │                                     │                       │
│   └─────────────────────────────────────┘                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Usage

```bash
./build/fuse_test <data.csv> [threshold] [--quiet]
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `data.csv` | (required) | CSV file to test |
| `threshold` | 0.1123 | Anomaly detection threshold |
| `--quiet` | off | Only show summary |

---

## Output

### Sample Output

```
╔══════════════════════════════════════════════════════════════╗
║  THE FUSE BOX — Universal Chip Tester                        ║
║  "Does your chip detect anomalies?"                          ║
╚══════════════════════════════════════════════════════════════╝

Chip:      CPU_CHIP
Data:      cpu_attack.csv (283 samples)
Threshold: 1.5000000000
AGC:       mean=9.7429, std=4.1295

═══════════════════════════════════════════════════════════════
   Time |      Raw |     Norm |   Output |    Delta | Status
═══════════════════════════════════════════════════════════════
      0 |    14.00 |   1.0309 |   0.1762 |   0.8547 | WARM
      1 |    11.00 |   0.3044 |  -0.3944 |   0.6988 | WARM
     ...
     85 |    24.00 |   3.4525 |   4.9615 |   1.5089 | >>ZIT
     86 |    23.00 |   3.2104 |   6.6213 |   3.4109 | >>ZIT
     ...

═══════════════════════════════════════════════════════════════
SUMMARY
═══════════════════════════════════════════════════════════════

Samples analyzed: 233 (after 50 warmup)

Delta Statistics:
  Min:  0.000371
  Max:  1275.656860
  Mean: 19.730303
  Std:  124.726334

Detection:
  Threshold: 1.5000000000
  Zits:      20 (8.58%)

Tracking Quality:
  Correlation (input vs output): 0.5899

═══════════════════════════════════════════════════════════════
VERDICT: SIGNIFICANT ANOMALIES — 20 Zits detected (8.6%)
         Pattern has changed from training data.
═══════════════════════════════════════════════════════════════
```

### Columns

| Column | Description |
|--------|-------------|
| Time | Sample index |
| Raw | Original sensor value |
| Norm | Normalized value (AGC applied) |
| Output | Chip prediction |
| Delta | \|Norm - Output\| |
| Status | WARM (warmup), >>ZIT (anomaly), or blank |

### Verdicts

| Verdict | Zit Rate | Meaning |
|---------|----------|---------|
| ALL CLEAR | 0% | No anomalies detected |
| MINOR ANOMALIES | < 5% | Occasional deviations |
| SIGNIFICANT ANOMALIES | 5-20% | Pattern has changed |
| SYSTEM FAILURE | > 20% | Data unlike training |

---

## Choosing a Threshold

### The Second Star Constant

The default threshold (0.1122911624) was evolved for the V3 Efficient Species on clean sinusoidal signals. For real-world data:

```bash
# Step 1: Test chip on training data
./build/fuse_test training_data.csv --quiet

# Step 2: Note the mean and std delta
# Example: Mean=0.52, Std=0.81

# Step 3: Set threshold = mean + 2*std
./build/fuse_test test_data.csv 2.14
```

### Threshold Guidelines

| Signal Type | Suggested Threshold |
|-------------|---------------------|
| Clean sine | 0.112 (Second Star) |
| CPU load | 1.5 - 2.5 |
| Motor vibration | 2.0 - 3.0 |
| Network traffic | 1.0 - 2.0 |

---

## Building

The Fuse Box is compiled with the chip header included at build time:

```bash
# Using Makefile
make fuse-compile CHIP_FILE=my_chip.h CHIP_NAME=MY_CHIP

# Manual compilation
gcc -O3 -DCHIP=MY_CHIP -include my_chip.h fuse_box.c -o fuse_test -lm
```

### Required Macros

The chip header must define:

| Macro | Example | Description |
|-------|---------|-------------|
| `CHIP_step(raw, state)` | `MY_CHIP_step` | Execute one step |
| `CHIP_reset(state)` | `MY_CHIP_reset` | Reset state |
| `CHIP_REGS` | `MY_CHIP_REGS` | State array size |
| `CHIP_AGC_MEAN` | `MY_CHIP_AGC_MEAN` | Normalization mean |
| `CHIP_AGC_STD` | `MY_CHIP_AGC_STD` | Normalization std |

These are automatically generated by The Forge.

---

## Warmup Period

The first 50 samples are marked as "WARM" and excluded from statistics. This allows the chip's recurrent state to initialize.

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | No anomalies (ALL CLEAR) |
| 1 | Anomalies detected |

This enables scripting:

```bash
if ./build/fuse_test data.csv 1.5 --quiet; then
    echo "System normal"
else
    echo "ALERT: Anomalies detected"
fi
```

---

## Files

| File | Description |
|------|-------------|
| `bin/fuse_box.c` | Fuse Box implementation |

---

## See Also

- [FORGE.md](FORGE.md) — How to create chips
- [ENTROMORPHIC_OS.md](ENTROMORPHIC_OS.md) — Real-time monitoring

---

*"Does your chip detect anomalies?"*

*"It's all in the reflexes."*
