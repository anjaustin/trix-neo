"""
dataset.py — Dataset loaders for TriX chip training.

Synthetic: 4-class Gaussian clusters in int8 R^64. Always available.
CWRU:      Case Western Reserve bearing fault dataset (.mat files).
           Download from: https://engineering.case.edu/bearingdatacenter/download-data-file
           Expected files: Normal_0.mat, IR007_0.mat, OR007@6_0.mat, B007_0.mat

Both return (X: np.int8 [N, K], y: np.int64 [N]), labels: list[str]
"""

import numpy as np
from pathlib import Path


def load_synthetic(n_per_class: int = 500, input_dim: int = 64,
                   seed: int = 42) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """
    Generate 4 synthetic classes as Gaussian clusters in int8 R^input_dim.
    Classes are separated by 64 units (ensuring Hamming separation post-encoding).

    Returns:
        X: int8 [n_per_class*4, input_dim]
        y: int64 [n_per_class*4]
        labels: ['class_0', 'class_1', 'class_2', 'class_3']
    """
    rng = np.random.default_rng(seed)
    num_classes = 4
    # Centers spread across int8 range [-128, 127]
    centers = np.array([
        [-64.0] * input_dim,
        [+64.0] * input_dim,
        [-64.0] * (input_dim // 2) + [+64.0] * (input_dim // 2),
        [+64.0] * (input_dim // 2) + [-64.0] * (input_dim // 2),
    ])

    Xs, ys = [], []
    for cls in range(num_classes):
        samples = rng.normal(centers[cls], scale=16.0, size=(n_per_class, input_dim))
        Xs.append(np.clip(samples, -128, 127).astype(np.int8))
        ys.append(np.full(n_per_class, cls, dtype=np.int64))

    X = np.concatenate(Xs, axis=0)
    y = np.concatenate(ys, axis=0)
    idx = rng.permutation(len(X))
    labels = [f"class_{i}" for i in range(num_classes)]
    return X[idx], y[idx], labels


def _load_mat_signal(path: str) -> np.ndarray:
    """Load a CWRU .mat file, return the drive-end accelerometer signal."""
    from scipy.io import loadmat
    data = loadmat(path)
    # CWRU mat files use keys like 'X097_DE_time', 'X098_DE_time', etc.
    # Find the drive-end key
    de_keys = [k for k in data.keys() if 'DE_time' in k]
    if not de_keys:
        raise ValueError(f"No DE_time key found in {path}. Keys: {list(data.keys())}")
    signal = data[de_keys[0]].flatten().astype(np.float32)
    return signal


def _signal_to_windows(signal: np.ndarray, window_size: int = 64,
                        stride: int = 32, scale: float = 64.0) -> np.ndarray:
    """
    Slice signal into overlapping windows, normalize to int8 range.

    Args:
        signal:      1D float32 accelerometer signal
        window_size: samples per window (= input_dim = K)
        stride:      hop between windows
        scale:       divisor to map raw signal to [-127, 127]

    Returns:
        windows: int8 [N_windows, window_size]
    """
    windows = []
    for start in range(0, len(signal) - window_size, stride):
        w = signal[start:start + window_size]
        # Normalize: zero-mean per window, scale to int8
        w = w - w.mean()
        max_abs = np.max(np.abs(w)) + 1e-8
        w = w / max_abs * scale
        windows.append(np.clip(w, -127, 127).astype(np.int8))
    return np.array(windows, dtype=np.int8)


def load_cwru(data_dir: str, window_size: int = 64,
              stride: int = 32, max_windows: int = 2000,
              seed: int = 42) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """
    Load CWRU bearing dataset from directory.

    Expects these files (or similar — the loader searches for DE_time keys):
        Normal_0.mat      → class 0: normal
        IR007_0.mat       → class 1: inner race fault (7 mil)
        OR007@6_0.mat     → class 2: outer race fault (7 mil, 6 o'clock)
        B007_0.mat        → class 3: ball fault (7 mil)

    Args:
        data_dir:    directory containing .mat files
        window_size: samples per window (must be ≤64, divisible by 4)
        stride:      hop between windows
        max_windows: cap per class to balance dataset

    Returns:
        X: int8 [N, window_size], y: int64 [N], labels: list[str]
    """
    assert window_size <= 64 and window_size % 4 == 0, \
        "window_size must be ≤64 and divisible by 4"

    base = Path(data_dir)
    # Map class index to glob pattern (most CWRU file naming)
    file_patterns = [
        ("normal",      ["Normal_0.mat", "normal_0.mat", "Normal*.mat"]),
        ("inner_race",  ["IR007_0.mat",  "IR007*.mat"]),
        ("outer_race",  ["OR007@6_0.mat","OR007*.mat"]),
        ("ball",        ["B007_0.mat",   "B007*.mat"]),
    ]

    Xs, ys = [], []
    labels = []
    rng = np.random.default_rng(seed)

    for cls_idx, (label, patterns) in enumerate(file_patterns):
        mat_path = None
        for pat in patterns:
            found = list(base.glob(pat))
            if found:
                mat_path = str(found[0])
                break
        if mat_path is None:
            raise FileNotFoundError(
                f"Could not find {label} data in {data_dir}. "
                f"Tried patterns: {patterns}"
            )

        signal = _load_mat_signal(mat_path)
        windows = _signal_to_windows(signal, window_size, stride)

        # Cap and shuffle
        if len(windows) > max_windows:
            idx = rng.choice(len(windows), max_windows, replace=False)
            windows = windows[idx]

        Xs.append(windows)
        ys.append(np.full(len(windows), cls_idx, dtype=np.int64))
        labels.append(label)

    X = np.concatenate(Xs, axis=0)
    y = np.concatenate(ys, axis=0)
    idx = rng.permutation(len(X))
    return X[idx], y[idx], labels


def train_val_split(X: np.ndarray, y: np.ndarray, val_frac: float = 0.2,
                    seed: int = 42) -> tuple:
    """Split into train/val, stratified by class."""
    rng = np.random.default_rng(seed)
    classes = np.unique(y)
    train_idx, val_idx = [], []
    for cls in classes:
        idx = np.where(y == cls)[0]
        rng.shuffle(idx)
        n_val = max(1, int(len(idx) * val_frac))
        val_idx.extend(idx[:n_val])
        train_idx.extend(idx[n_val:])
    return (X[train_idx], y[train_idx],
            X[val_idx],   y[val_idx])
