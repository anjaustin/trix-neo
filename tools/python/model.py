"""
model.py — Binary Neural Network encoder for TriX chip training.

Architecture: Linear(K, hidden) → ReLU → Linear(hidden, N) → SignSTE → {0,1}^N
Training: float weights, STE through sign, classification loss + balance reg.
Export: quantize float weights to int8 (clamp to [-127, 127]).
"""

import torch
import torch.nn as nn
import numpy as np


class SignSTE(torch.autograd.Function):
    """Sign function with straight-through estimator for the backward pass."""

    @staticmethod
    def forward(ctx, x):
        # 1 where x > 0, 0 elsewhere — matches linear_sign_binarize() in C
        return (x > 0).float()

    @staticmethod
    def backward(ctx, grad_output):
        # Pass gradient straight through (STE)
        return grad_output


def sign_ste(x):
    return SignSTE.apply(x)


class BNNEncoder(nn.Module):
    """
    Binary metric encoder.

    Takes a K-dimensional int8 input, produces an N-dimensional binary code.
    N must be 512 (= state_bits = 64 bytes × 8 bits) for TriX compatibility.

    Args:
        input_dim:  K — number of input features (≤ 64, divisible by 4)
        hidden_dim: intermediate dimension (use 256 for single-layer, 0 to skip)
        output_dim: N — must be 512
        num_classes: number of output classes (for classification head)
    """

    def __init__(self, input_dim: int, hidden_dim: int, output_dim: int,
                 num_classes: int):
        super().__init__()
        assert output_dim == 512, "output_dim must be 512 for TriX"
        assert input_dim <= 64 and input_dim % 4 == 0, \
            "input_dim must be ≤64 and divisible by 4"

        self.input_dim = input_dim
        self.output_dim = output_dim

        # Encoder layers
        layers = []
        in_dim = input_dim
        if hidden_dim > 0:
            layers += [nn.Linear(in_dim, hidden_dim, bias=False), nn.ReLU()]
            in_dim = hidden_dim
        layers += [nn.Linear(in_dim, output_dim, bias=False)]
        self.encoder = nn.Sequential(*layers)

        # Classification head (for supervised training signal)
        self.classifier = nn.Linear(output_dim, num_classes)

    def encode(self, x: torch.Tensor) -> torch.Tensor:
        """
        Forward through encoder, return float pre-binarization activations.
        Shape: (batch, output_dim)
        """
        return self.encoder(x.float())

    def binary_code(self, x: torch.Tensor) -> torch.Tensor:
        """
        Return binary code {0,1}^N via STE. Use during training.
        Shape: (batch, output_dim)
        """
        z = self.encode(x)
        return sign_ste(z)

    def forward(self, x: torch.Tensor):
        """
        Returns (binary_code, logits) for training.
        binary_code: (batch, 512) float {0.0, 1.0} via STE
        logits: (batch, num_classes) for cross-entropy
        """
        z = self.encode(x)
        code = sign_ste(z)
        logits = self.classifier(code)
        return code, logits

    def export_weights_int8(self) -> list[np.ndarray]:
        """
        Return list of int8 weight arrays, one per encoder linear layer.
        Shape: [N, K] row-major int8 — matches runtime expectation.
        Quantization: scale each layer to fill int8 range, clamp to [-127, 127].
        """
        weights = []
        for module in self.encoder:
            if isinstance(module, nn.Linear):
                W = module.weight.detach().float().numpy()  # [N, K]
                scale = 127.0 / (np.max(np.abs(W)) + 1e-8)
                W_q = np.clip(np.round(W * scale), -127, 127).astype(np.int8)
                weights.append(W_q)
        return weights
