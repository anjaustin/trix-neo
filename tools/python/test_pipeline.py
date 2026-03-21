"""
test_pipeline.py — End-to-end training → export → inference validation.

Trains on synthetic data, exports chip, loads via C runtime, runs inference,
verifies that class centers match their expected signatures.

Usage: python3 tools/python/test_pipeline.py
Exit code 0 = pass, 1 = fail.
"""

import sys
import os
import numpy as np
import torch

sys.path.insert(0, os.path.dirname(__file__))
from model import BNNEncoder
from dataset import load_synthetic, train_val_split
from export import export_chip
from train import train_epoch, eval_epoch, balance_loss
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset


def main():
    print("=== Training pipeline end-to-end test ===\n")

    # 1. Train for 30 epochs on synthetic data
    X, y, labels = load_synthetic(n_per_class=300, input_dim=64)
    Xtr, ytr, Xv, yv = train_val_split(X, y)

    model = BNNEncoder(64, 256, 512, 4)
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)
    train_ds = DataLoader(
        TensorDataset(torch.tensor(Xtr, dtype=torch.float32),
                      torch.tensor(ytr, dtype=torch.long)),
        batch_size=64, shuffle=True
    )
    val_ds = DataLoader(
        TensorDataset(torch.tensor(Xv, dtype=torch.float32),
                      torch.tensor(yv, dtype=torch.long)),
        batch_size=64
    )

    best_acc = 0.0
    best_state = None
    for epoch in range(30):
        train_epoch(model, train_ds, optimizer, 0.1, "cpu")
        acc = eval_epoch(model, val_ds, "cpu")
        if acc > best_acc:
            best_acc = acc
            best_state = {k: v.clone() for k, v in model.state_dict().items()}

    model.load_state_dict(best_state)
    print(f"Training complete. Best val acc: {best_acc:.3f}")

    assert best_acc > 0.80, f"FAIL: val acc {best_acc:.3f} < 0.80"
    print("  ✓ Val accuracy > 0.80")

    # 2. Export chip
    out_dir = "/tmp/trix_pipeline_test"
    trix_path = export_chip(model, Xtr, ytr, labels,
                            out_dir=out_dir, chip_name="pipeline_test")
    assert os.path.exists(trix_path), f"FAIL: {trix_path} not created"
    assert os.path.exists(trix_path.replace(".trix", "_layer0.bin")), "FAIL: weight file missing"
    print(f"  ✓ Chip exported to {trix_path}")

    # 3. Load via Python ctypes and infer class centers
    import trix as trix_py
    if not trix_py.is_available():
        print("  SKIP: trix runtime not built (run cmake --build build first)")
        return 0

    chip = trix_py.load(trix_path)
    print(f"  ✓ Chip loaded: {chip.name}, {chip.num_signatures} signatures")

    # Create class center inputs (same as dataset.load_synthetic centers)
    centers_int8 = np.array([
        np.full(64, -64, dtype=np.int8),
        np.full(64, +64, dtype=np.int8),
        np.array([-64]*32 + [64]*32, dtype=np.int8),
        np.array([64]*32 + [-64]*32, dtype=np.int8),
    ])

    matches = 0
    for cls_idx, center in enumerate(centers_int8):
        result = chip.infer(bytes(center))
        expected_label = labels[cls_idx]
        if result.is_match and result.label == expected_label:
            matches += 1
            print(f"  ✓ class_center[{cls_idx}] → {result.label} dist={result.distance}")
        else:
            print(f"  ✗ class_center[{cls_idx}] → {result.label or 'no match'} "
                  f"(expected {expected_label}), dist={result.distance}")

    assert matches >= 3, f"FAIL: only {matches}/4 class centers matched correctly"
    print(f"\n  ✓ {matches}/4 class centers correctly classified")
    print("\nPASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
