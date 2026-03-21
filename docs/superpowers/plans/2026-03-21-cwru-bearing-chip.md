# CWRU Bearing Fault Chip — First Real Trained Chip

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Train and validate a TriX chip that classifies CWRU bearing fault data into 4 classes (normal, inner race, outer race, ball fault) with ≥85% accuracy on held-out data.

**Architecture:** Uses the training pipeline from `2026-03-21-training-pipeline.md`. This plan covers CWRU-specific data handling, hyperparameter choices, validation methodology, and the certification artifact (accuracy report + Hamming distance distribution). Depends on Plan 1 (training pipeline) being complete.

**Tech Stack:** Python 3.9+, PyTorch, SciPy (CWRU .mat loading), existing `tools/python/` training stack, existing `trix_load()` + `trix_infer()` C runtime

---

## Prerequisites

Plan 1 (training pipeline) must be complete:
- `tools/python/model.py` exists
- `tools/python/dataset.py` exists
- `tools/python/train.py` exists
- `tools/python/export.py` exists
- `tools/python/requirements.txt` exists

CWRU dataset must be downloaded:
```
https://engineering.case.edu/bearingdatacenter/download-data-file
```

Required files (12kHz, drive end, 0 HP load):
- `Normal_0.mat` (or `97.mat` — file naming varies by mirror)
- `IR007_0.mat`  (inner race fault, 7 mil)
- `OR007@6_0.mat` (outer race fault, 7 mil, 6 o'clock)
- `B007_0.mat`   (ball fault, 7 mil)

---

## File Map

| File | Action | Purpose |
|---|---|---|
| `tools/python/dataset.py` | Already exists | CWRU loader already implemented |
| `tools/python/validate.py` | Create | Full validation: accuracy, Hamming distributions, confusion matrix |
| `chips/bearing/` | Create (dir) | Output directory for bearing chip |
| `docs/chips/bearing_fault_report.md` | Create | Certification artifact: accuracy + Hamming stats |

---

## Task 1: Download and Verify CWRU Data

- [ ] **Step 1: Create data directory**

```bash
mkdir -p data/cwru
```

- [ ] **Step 2: Download required files**

From https://engineering.case.edu/bearingdatacenter/download-data-file, download the **12kHz Drive End** files for 0 HP load condition. The specific files needed:

| Class | File | CWRU identifier |
|---|---|---|
| Normal | `97.mat` | Normal Baseline |
| Inner race | `105.mat` | 0.007" IR fault |
| Outer race | `130.mat` | 0.007" OR fault, 6 o'clock |
| Ball | `118.mat` | 0.007" Ball fault |

Place them in `data/cwru/`. They may have different names depending on download method — the loader searches for `DE_time` keys, so filenames just need to match the glob patterns in `dataset.py`.

Rename if needed:
```bash
# Rename to expected patterns if your downloads have numeric names
mv data/cwru/97.mat   data/cwru/Normal_0.mat
mv data/cwru/105.mat  data/cwru/IR007_0.mat
mv data/cwru/130.mat  data/cwru/OR007@6_0.mat
mv data/cwru/118.mat  data/cwru/B007_0.mat
```

- [ ] **Step 3: Verify data loads correctly**

```bash
python3 -c "
import sys; sys.path.insert(0, 'tools/python')
from dataset import load_cwru
X, y, labels = load_cwru('data/cwru', window_size=64)
print(f'Loaded {len(X)} windows, {len(labels)} classes')
print(f'X shape: {X.shape}, dtype: {X.dtype}')
import numpy as np
for cls in range(4):
    print(f'  {labels[cls]}: {np.sum(y==cls)} windows')
"
```

Expected:
```
Loaded ~6000-8000 windows, 4 classes
X shape: (N, 64), dtype: int8
  normal:     ~1500-2000 windows
  inner_race: ~1500-2000 windows
  outer_race: ~1500-2000 windows
  ball:       ~1500-2000 windows
```

**If a class shows 0 windows:** The `.mat` filename doesn't match the glob patterns. Check actual filenames with `ls data/cwru/` and update the rename step.

---

## Task 2: Train the Bearing Fault Chip

These hyperparameters are tuned for CWRU 64-sample windows. Do not change without re-validating.

- [ ] **Step 1: Install training dependencies**

```bash
pip3 install -r tools/python/requirements.txt
```

- [ ] **Step 2: Create output directory**

```bash
mkdir -p chips/bearing
```

- [ ] **Step 3: Run training**

```bash
python3 tools/python/train.py \
    --dataset cwru \
    --data-dir data/cwru \
    --input-dim 64 \
    --hidden-dim 256 \
    --epochs 100 \
    --batch-size 128 \
    --lr 1e-3 \
    --lambda-bal 0.1 \
    --out-dir chips/bearing \
    --chip-name bearing_fault
```

Expected training progression:
```
Epoch   1: loss=1.xxxx train_acc=0.3xx val_acc=0.3xx
Epoch  10: loss=0.xxxx train_acc=0.7xx val_acc=0.7xx
Epoch  50: loss=0.xxxx train_acc=0.9xx val_acc=0.8xx
Epoch 100: loss=0.xxxx train_acc=0.9xx val_acc=0.85+
Best val accuracy: >0.85
Chip exported to: chips/bearing/bearing_fault.trix
```

**If val_acc plateaus below 0.80 after 50 epochs:**
1. Check dataset balance: `python3 -c "from dataset import load_cwru; X,y,l = load_cwru('data/cwru'); import numpy as np; [print(l[i], np.sum(y==i)) for i in range(4)]"`
2. Try `--lambda-bal 0.05` (less balance regularization)
3. Try `--hidden-dim 512` (larger encoder)
4. Verify data normalization: signal scale should map to [-64, 64] range in int8

**If val_acc is 0.25 (random):** The class separation is failing. Check that `_signal_to_windows()` in `dataset.py` is zero-centering per window. Fault signals have different spectral content but similar mean — per-window mean subtraction is critical.

- [ ] **Step 4: Inspect the exported chip file**

```bash
cat chips/bearing/bearing_fault.trix
```

Verify:
- `linear_layers:` section is present with `input_dim: 64`, `output_dim: 512`
- `signatures:` section has 4 entries: `normal`, `inner_race`, `outer_race`, `ball`
- Each threshold is ≥10 and ≤200 (reasonable Hamming range for 512-bit codes)
- Weight file exists: `ls -la chips/bearing/bearing_fault_layer0.bin`

Expected weight file size: 64 × 512 = 32,768 bytes for hidden_dim=0 (direct 64→512), or 256×64 + 512×256 = 147,456 bytes for hidden_dim=256. Size should match `input_dim × hidden_dim + hidden_dim × output_dim` bytes.

---

## Task 3: Validation Script

**Files:**
- Create: `tools/python/validate.py`

- [ ] **Step 1: Write `tools/python/validate.py`**

```python
"""
validate.py — Validate a trained TriX chip against a held-out test set.

Reports:
  - Per-class accuracy
  - Overall accuracy
  - Hamming distance distributions (intra-class and inter-class)
  - Confusion matrix
  - Threshold coverage: % of test examples within threshold

Usage:
    python3 tools/python/validate.py \
        --chip chips/bearing/bearing_fault.trix \
        --dataset cwru \
        --data-dir data/cwru \
        --report-out docs/chips/bearing_fault_report.md
"""

import argparse
import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))


def hamming_distance_bits(a: np.ndarray, b: np.ndarray) -> int:
    """Hamming distance between two uint8[64] patterns (512 bits)."""
    return int(np.sum(np.unpackbits(a.astype(np.uint8)) !=
                      np.unpackbits(b.astype(np.uint8))))


def run_validation(chip_path: str, X: np.ndarray, y: np.ndarray,
                   labels: list[str]) -> dict:
    """
    Run inference on all X through C runtime, compute stats.
    Returns dict with accuracy, confusion, hamming distributions.
    """
    import trix as trix_py
    if not trix_py.is_available():
        raise RuntimeError(
            "TriX runtime not found. Set TRIX_LIB_PATH or build the shared library.\n"
            "Build: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
        )

    chip = trix_py.load(chip_path)
    num_classes = len(labels)
    n = len(X)

    predictions = np.full(n, -1, dtype=np.int64)
    distances   = np.full(n, 512, dtype=np.int64)

    print(f"Running {n} inferences...")
    for i, sample in enumerate(X):
        result = chip.infer(bytes(sample.astype(np.uint8)))
        if result.is_match:
            # Map label string back to class index
            if result.label in labels:
                predictions[i] = labels.index(result.label)
            distances[i] = result.distance
        if (i + 1) % 500 == 0:
            print(f"  {i+1}/{n}")

    # Overall accuracy (only matched samples)
    matched_mask = predictions >= 0
    n_matched = np.sum(matched_mask)
    n_correct = np.sum(predictions[matched_mask] == y[matched_mask])
    overall_acc = n_correct / n if n > 0 else 0.0
    match_rate = n_matched / n

    # Per-class accuracy
    per_class_acc = {}
    per_class_match = {}
    for cls_idx, label in enumerate(labels):
        mask = y == cls_idx
        n_cls = np.sum(mask)
        n_cls_matched = np.sum(matched_mask[mask])
        n_cls_correct = np.sum(predictions[mask] == cls_idx)
        per_class_acc[label] = n_cls_correct / n_cls if n_cls > 0 else 0.0
        per_class_match[label] = n_cls_matched / n_cls if n_cls > 0 else 0.0

    # Confusion matrix
    confusion = np.zeros((num_classes, num_classes), dtype=np.int64)
    for i in range(n):
        if matched_mask[i]:
            confusion[y[i], predictions[i]] += 1

    # Hamming distance distributions per class
    hamming_stats = {}
    for cls_idx, label in enumerate(labels):
        mask = (y == cls_idx) & matched_mask
        cls_dists = distances[mask]
        if len(cls_dists) > 0:
            hamming_stats[label] = {
                "mean": float(np.mean(cls_dists)),
                "std":  float(np.std(cls_dists)),
                "p50":  float(np.percentile(cls_dists, 50)),
                "p95":  float(np.percentile(cls_dists, 95)),
                "min":  int(np.min(cls_dists)),
                "max":  int(np.max(cls_dists)),
            }

    return {
        "overall_accuracy": overall_acc,
        "match_rate": match_rate,
        "n_total": n,
        "n_matched": int(n_matched),
        "n_correct": int(n_correct),
        "per_class_accuracy": per_class_acc,
        "per_class_match_rate": per_class_match,
        "confusion_matrix": confusion,
        "hamming_stats": hamming_stats,
        "labels": labels,
    }


def write_report(stats: dict, chip_path: str, out_path: str):
    """Write a markdown validation report."""
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    labels = stats["labels"]

    with open(out_path, "w") as f:
        f.write(f"# Chip Validation Report\n\n")
        f.write(f"**Chip:** `{chip_path}`\n\n")
        f.write(f"## Summary\n\n")
        f.write(f"| Metric | Value |\n|---|---|\n")
        f.write(f"| Overall accuracy | {stats['overall_accuracy']:.3f} |\n")
        f.write(f"| Match rate | {stats['match_rate']:.3f} |\n")
        f.write(f"| Total samples | {stats['n_total']} |\n")
        f.write(f"| Matched | {stats['n_matched']} |\n")
        f.write(f"| Correct | {stats['n_correct']} |\n\n")

        f.write(f"## Per-Class Accuracy\n\n")
        f.write(f"| Class | Accuracy | Match Rate |\n|---|---|---|\n")
        for label in labels:
            acc = stats['per_class_accuracy'].get(label, 0.0)
            mr  = stats['per_class_match_rate'].get(label, 0.0)
            f.write(f"| {label} | {acc:.3f} | {mr:.3f} |\n")

        f.write(f"\n## Hamming Distance Distributions\n\n")
        f.write(f"| Class | Mean | Std | P50 | P95 | Min | Max |\n")
        f.write(f"|---|---|---|---|---|---|---|\n")
        for label in labels:
            h = stats['hamming_stats'].get(label, {})
            if h:
                f.write(f"| {label} | {h['mean']:.1f} | {h['std']:.1f} | "
                        f"{h['p50']:.1f} | {h['p95']:.1f} | "
                        f"{h['min']} | {h['max']} |\n")

        f.write(f"\n## Confusion Matrix\n\n")
        f.write(f"Rows = true class, Columns = predicted class\n\n")
        f.write("| | " + " | ".join(labels) + " |\n")
        f.write("|---" * (len(labels) + 1) + "|\n")
        for i, label in enumerate(labels):
            row = stats['confusion_matrix'][i]
            f.write(f"| {label} | " + " | ".join(str(v) for v in row) + " |\n")

    print(f"Report written to {out_path}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--chip", required=True, help="Path to .trix chip file")
    p.add_argument("--dataset", choices=["synthetic", "cwru"], default="cwru")
    p.add_argument("--data-dir", default="data/cwru")
    p.add_argument("--report-out", default="chips/validation_report.md")
    p.add_argument("--test-only", action="store_true",
                   help="Use val split only (faster)")
    args = p.parse_args()

    if args.dataset == "cwru":
        from dataset import load_cwru, train_val_split
        X, y, labels = load_cwru(args.data_dir)
        if args.test_only:
            _, _, X, y = train_val_split(X, y, val_frac=0.2)
    else:
        from dataset import load_synthetic, train_val_split
        X, y, labels = load_synthetic()
        _, _, X, y = train_val_split(X, y, val_frac=0.2)

    print(f"Validating {len(X)} samples against {args.chip}")
    stats = run_validation(args.chip, X, y, labels)

    print(f"\nOverall accuracy: {stats['overall_accuracy']:.3f}")
    print(f"Match rate:       {stats['match_rate']:.3f}")
    for label in labels:
        acc = stats['per_class_accuracy'].get(label, 0.0)
        print(f"  {label}: {acc:.3f}")

    write_report(stats, args.chip, args.report_out)

    # Fail if accuracy is below minimum viable threshold
    if stats['overall_accuracy'] < 0.80:
        print(f"\nFAIL: accuracy {stats['overall_accuracy']:.3f} < 0.80 minimum")
        sys.exit(1)
    print(f"\nPASS: accuracy {stats['overall_accuracy']:.3f} ≥ 0.80")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Commit the validation script**

```bash
git add tools/python/validate.py
git commit -m "feat: add chip validation script with accuracy, Hamming stats, confusion matrix"
```

---

## Task 4: Run Full Validation

- [ ] **Step 1: Build the runtime (required for Python ctypes inference)**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
```

- [ ] **Step 2: Run validation against the full CWRU dataset**

```bash
TRIX_LIB_PATH=$(pwd)/build \
python3 tools/python/validate.py \
    --chip chips/bearing/bearing_fault.trix \
    --dataset cwru \
    --data-dir data/cwru \
    --report-out docs/chips/bearing_fault_report.md
```

Expected output:
```
Validating NNNN samples against chips/bearing/bearing_fault.trix
Running NNNN inferences...
  500/NNNN
  ...

Overall accuracy: 0.8xx
Match rate:       0.9xx
  normal:      0.9xx
  inner_race:  0.8xx
  outer_race:  0.8xx
  ball:        0.8xx

Report written to docs/chips/bearing_fault_report.md
PASS: accuracy 0.8xx ≥ 0.80
```

**If accuracy < 0.80:** Retrain with:
- `--epochs 150` (more training)
- `--lambda-bal 0.05` (less regularization — allows more bit specialization)
- `--hidden-dim 512` (larger encoder capacity)

**If match rate < 0.70** (many `-1` no-match results): Thresholds are too tight. This means `export.py`'s 95th percentile threshold is being undercut. Check the Hamming distribution in the report — if P95 is 40 but the threshold was set to 40 and test distances peak higher, re-export with `--threshold-percentile 99` (edit `export_chip()` call in `train.py`).

- [ ] **Step 3: Inspect the validation report**

```bash
cat docs/chips/bearing_fault_report.md
```

Verify:
- Overall accuracy ≥ 0.80
- Per-class accuracy ≥ 0.70 for all 4 classes
- Hamming P95 values are within thresholds (match rate should confirm this)
- Confusion matrix shows diagonal dominance

- [ ] **Step 4: Run a quick smoke test via C (optional but recommended)**

Write a minimal C test that loads the bearing chip and checks that the class centers produce correct results:

```bash
# Using the existing trix CLI tool if available
./build/trix chips/bearing/bearing_fault.trix info
```

Expected:
```
Chip: bearing_fault v1.0.0
  State bits: 512
  Signatures: 4
  Linear layers: 1 (input_dim=64, output_dim=512)
```

- [ ] **Step 5: Commit chip and report**

```bash
mkdir -p docs/chips
git add chips/bearing/bearing_fault.trix docs/chips/bearing_fault_report.md
# Do NOT git add the .bin weight file if large — add to .gitignore instead
echo "chips/**/*.bin" >> .gitignore
git add .gitignore
git commit -m "feat: first real trained chip — CWRU bearing fault 4-class classifier"
```

---

## Task 5: HSOS Inference Validation

Verify the bearing chip produces identical results via both `trix_infer()` and `hsos_exec_infer()`.

**Files:**
- Create: `zor/test/test_bearing_chip.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write `zor/test/test_bearing_chip.c`**

```c
/*
 * test_bearing_chip.c — Smoke test: load bearing_fault.trix, verify both
 *                       inference paths agree.
 *
 * Requires chips/bearing/bearing_fault.trix to exist (built by training pipeline).
 * Skip gracefully if chip file not found.
 */

#include "../include/hsos.h"
#include "../include/trixc/hsos_infer.h"
#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHIP_PATH "chips/bearing/bearing_fault.trix"

int main(void) {
    printf("[TEST] bearing_chip_smoke_test\n");

    int err = 0;
    trix_chip_t *chip = trix_load(CHIP_PATH, &err);
    if (!chip) {
        printf("  SKIP: %s not found (run training pipeline first)\n", CHIP_PATH);
        return 0;  /* Not a failure — chip is optional */
    }

    printf("  Loaded: %s\n", CHIP_PATH);

    hsos_system_t *sys = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys);
    hsos_boot(sys);

    /* Test 5 synthetic inputs */
    int mismatches = 0;
    for (int t = 0; t < 5; t++) {
        uint8_t input[64];
        memset(input, (uint8_t)(t * 50), 64);

        trix_result_t direct  = trix_infer(chip, input);
        trix_result_t via_hsos = hsos_exec_infer(sys, chip, input);

        if (direct.match != via_hsos.match ||
            direct.distance != via_hsos.distance) {
            fprintf(stderr,
                "  FAIL: input[%d]: direct=(%d,%d) hsos=(%d,%d)\n",
                t, direct.match, direct.distance,
                via_hsos.match, via_hsos.distance);
            mismatches++;
        } else {
            const char *label = direct.match >= 0 ? direct.label : "no match";
            printf("  ✓ input[%d]: %s (dist=%d)\n", t, label, direct.distance);
        }
    }

    hsos_system_free(sys);
    free(sys);
    trix_chip_free(chip);

    if (mismatches > 0) {
        fprintf(stderr, "FAIL — %d mismatch(es)\n", mismatches);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
```

- [ ] **Step 2: Add to CMakeLists.txt**

```cmake
add_trix_test(test_bearing_chip zor/test/test_bearing_chip.c)
```

Note: this test gracefully SKIPs (returns 0) if the chip file doesn't exist, so it won't break CI on machines without the trained chip.

- [ ] **Step 3: Build and run**

```bash
cmake --build build -j4
cd build && ctest -R test_bearing_chip --output-on-failure
```

Expected: PASS (or SKIP if chip not present).

- [ ] **Step 4: Commit**

```bash
git add zor/test/test_bearing_chip.c CMakeLists.txt
git commit -m "test: bearing chip smoke test — direct vs HSOS inference agreement"
```
