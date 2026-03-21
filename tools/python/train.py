"""
train.py — Training loop for TriX BNN encoder.

Loss = CrossEntropy(logits, y) + lambda_bal * BalanceLoss(code)

BalanceLoss pushes each bit toward 50% activation across the batch:
    balance_loss = mean((mean(code, dim=0) - 0.5)^2)
This prevents code collapse (all-zeros or all-ones).

Usage:
    python3 train.py --dataset synthetic --out-dir chips/
    python3 train.py --dataset cwru --data-dir /path/to/cwru --out-dir chips/
"""

import argparse
import sys
import os
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, TensorDataset

sys.path.insert(0, os.path.dirname(__file__))
from model import BNNEncoder
from dataset import load_synthetic, load_cwru, train_val_split
from export import export_chip


def balance_loss(code: torch.Tensor) -> torch.Tensor:
    """Penalize bits that are always 0 or always 1 across the batch."""
    mean_activation = code.mean(dim=0)          # [N]
    return ((mean_activation - 0.5) ** 2).mean()


def train_epoch(model, loader, optimizer, lambda_bal: float, device: str):
    model.train()
    total_loss = 0.0
    correct = 0
    total = 0
    ce = nn.CrossEntropyLoss()

    for X_batch, y_batch in loader:
        X_batch = X_batch.to(device).float()
        y_batch = y_batch.to(device)

        optimizer.zero_grad()
        code, logits = model(X_batch)

        loss = ce(logits, y_batch) + lambda_bal * balance_loss(code)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * len(X_batch)
        correct += (logits.argmax(dim=1) == y_batch).sum().item()
        total += len(X_batch)

    return total_loss / total, correct / total


@torch.no_grad()
def eval_epoch(model, loader, device: str):
    model.eval()
    correct = 0
    total = 0
    for X_batch, y_batch in loader:
        X_batch = X_batch.to(device).float()
        y_batch = y_batch.to(device)
        _, logits = model(X_batch)
        correct += (logits.argmax(dim=1) == y_batch).sum().item()
        total += len(X_batch)
    return correct / total


def main():
    p = argparse.ArgumentParser(description="Train TriX BNN encoder")
    p.add_argument("--dataset",    choices=["synthetic", "cwru"], default="synthetic")
    p.add_argument("--data-dir",   default=".", help="CWRU .mat directory")
    p.add_argument("--input-dim",  type=int, default=64)
    p.add_argument("--hidden-dim", type=int, default=256)
    p.add_argument("--epochs",     type=int, default=50)
    p.add_argument("--batch-size", type=int, default=128)
    p.add_argument("--lr",         type=float, default=1e-3)
    p.add_argument("--lambda-bal", type=float, default=0.1,
                   help="Balance regularization weight")
    p.add_argument("--out-dir",    default="chips")
    p.add_argument("--chip-name",  default="trix_chip")
    args = p.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Device: {device}")

    # Load data
    if args.dataset == "synthetic":
        X, y, labels = load_synthetic(n_per_class=500, input_dim=args.input_dim)
    else:
        X, y, labels = load_cwru(args.data_dir, window_size=args.input_dim)
    print(f"Dataset: {len(X)} examples, {len(labels)} classes: {labels}")

    Xtr, ytr, Xv, yv = train_val_split(X, y, val_frac=0.2)
    print(f"Train: {len(Xtr)}, Val: {len(Xv)}")

    # Dataloaders
    train_ds = TensorDataset(
        torch.tensor(Xtr, dtype=torch.float32),
        torch.tensor(ytr, dtype=torch.long)
    )
    val_ds = TensorDataset(
        torch.tensor(Xv, dtype=torch.float32),
        torch.tensor(yv, dtype=torch.long)
    )
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True)
    val_loader   = DataLoader(val_ds,   batch_size=args.batch_size, shuffle=False)

    # Model
    model = BNNEncoder(
        input_dim=args.input_dim,
        hidden_dim=args.hidden_dim,
        output_dim=512,
        num_classes=len(labels)
    ).to(device)
    optimizer = optim.Adam(model.parameters(), lr=args.lr)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    # Train
    best_val_acc = 0.0
    best_state = None
    for epoch in range(1, args.epochs + 1):
        train_loss, train_acc = train_epoch(
            model, train_loader, optimizer, args.lambda_bal, device)
        val_acc = eval_epoch(model, val_loader, device)
        scheduler.step()

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state = {k: v.clone() for k, v in model.state_dict().items()}

        if epoch % 10 == 0 or epoch == 1:
            print(f"Epoch {epoch:3d}: loss={train_loss:.4f} "
                  f"train_acc={train_acc:.3f} val_acc={val_acc:.3f}")

    # Restore best
    model.load_state_dict(best_state)
    print(f"\nBest val accuracy: {best_val_acc:.3f}")

    # Export
    print("\nExporting chip...")
    trix_path = export_chip(
        model, Xtr, ytr, labels,
        out_dir=args.out_dir,
        chip_name=args.chip_name
    )
    print(f"Chip exported to: {trix_path}")


if __name__ == "__main__":
    main()
