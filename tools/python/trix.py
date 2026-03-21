"""
trix - Python bindings for TriX Deterministic AI Runtime

Usage:
    import trix
    chip = trix.load("model.trix")
    result = chip.infer(b"..." * 64)
    print(result.label, result.distance)
"""

import ctypes
import os
from pathlib import Path


# Find the TriX shared library
def _find_lib():
    """Find the TriX shared library."""
    # Check various possible locations
    lib_names = [
        "libtrix_runtime.so",  # Linux
        "libtrix_runtime.dylib",  # macOS
        "libtrix_runtime.0.1.0.dylib",  # macOS versioned
        "trix_runtime.dll",  # Windows
    ]

    # Try environment variable
    if "TRIX_LIB_PATH" in os.environ:
        for name in lib_names:
            path = os.path.join(os.environ["TRIX_LIB_PATH"], name)
            if os.path.exists(path):
                return path

    # Try relative to this file
    lib_dir = Path(__file__).parent / "lib"
    if lib_dir.exists():
        for name in lib_names:
            path = lib_dir / name
            if path.exists():
                return str(path)

    # Try system paths
    for name in lib_names:
        for path in [f"/usr/local/lib/{name}", f"/usr/lib/{name}"]:
            if os.path.exists(path):
                return path

    return None


# Load the library
_lib = None
try:
    _lib_path = _find_lib()
    if _lib_path:
        _lib = ctypes.CDLL(_lib_path)
except Exception as e:
    pass

class _TrixResult(ctypes.Structure):
    _fields_ = [
        ("match", ctypes.c_int),
        ("distance", ctypes.c_int),
        ("threshold", ctypes.c_int),
        ("label", ctypes.c_char_p),
        ("trace_tick_start", ctypes.c_uint32),
        ("trace_tick_end", ctypes.c_uint32),
    ]


class _TrixChipInfo(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("version", ctypes.c_char_p),
        ("state_bits", ctypes.c_int),
        ("num_signatures", ctypes.c_int),
        ("num_shapes", ctypes.c_int),
        ("num_linear_layers", ctypes.c_int),
    ]


# ctypes bindings
if _lib is not None:
    # trix_load(filename, error)
    _lib.trix_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
    _lib.trix_load.restype = ctypes.c_void_p

    # trix_infer(chip, input) -> trix_result_t
    _lib.trix_infer.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8)]
    _lib.trix_infer.restype = _TrixResult

    # trix_info(chip) -> const trix_chip_info_t*
    _lib.trix_info.argtypes = [ctypes.c_void_p]
    _lib.trix_info.restype = ctypes.POINTER(_TrixChipInfo)

    # trix_chip_free(chip)
    _lib.trix_chip_free.argtypes = [ctypes.c_void_p]
    _lib.trix_chip_free.restype = None

    # trix_label(chip, index)
    _lib.trix_label.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.trix_label.restype = ctypes.c_char_p

    # trix_threshold(chip, index)
    _lib.trix_threshold.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.trix_threshold.restype = ctypes.c_int

    # trix_signature(chip, index)
    _lib.trix_signature.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.trix_signature.restype = ctypes.POINTER(ctypes.c_uint8)


class Result:
    """Inference result."""

    def __init__(self, match, distance, threshold, label,
                 trace_tick_start=0, trace_tick_end=0):
        self.match = match
        self.distance = distance
        self.threshold = threshold
        self.label = label
        self.label_bytes = label
        self.trace_tick_start = trace_tick_start
        self.trace_tick_end = trace_tick_end

    @property
    def is_match(self):
        return self.match >= 0

    def __repr__(self):
        if self.is_match:
            return f"<Result: {self.label} (distance={self.distance}/{self.threshold})>"
        return f"<Result: no match (distance={self.distance})>"


class Chip:
    """TriX chip loaded from .trix file."""

    def __init__(self, path, lib=_lib):
        self._lib = lib
        if lib is None:
            raise ImportError("TriX runtime library not found")

        # Convert path to bytes
        if isinstance(path, str):
            path = path.encode("utf-8")

        error = ctypes.c_int(0)
        self._chip = lib.trix_load(path, ctypes.byref(error))

        if not self._chip:
            raise RuntimeError(f"Failed to load {path}: error {error.value}")

        # Get info
        info_ptr = lib.trix_info(self._chip)
        if not info_ptr:
            raise RuntimeError("Failed to get chip info")

        info = info_ptr.contents
        self._name = info.name.decode("utf-8") if info.name else ""
        self._version = info.version.decode("utf-8") if info.version else ""
        self._state_bits = info.state_bits
        self._num_signatures = info.num_signatures
        self._num_shapes = info.num_shapes
        self._num_linear_layers = info.num_linear_layers

    @property
    def name(self):
        return self._name

    @property
    def version(self):
        return self._version

    @property
    def state_bits(self):
        return self._state_bits

    @property
    def num_signatures(self):
        return self._num_signatures

    @property
    def signatures(self):
        return [self.signature(i) for i in range(self._num_signatures)]

    def signature(self, index):
        """Get signature at index."""
        if index < 0 or index >= self._num_signatures:
            return None
        label = self._lib.trix_label(self._chip, index)
        if label:
            label = label.decode("utf-8")
        threshold = self._lib.trix_threshold(self._chip, index)

        sig_ptr = self._lib.trix_signature(self._chip, index)
        pattern = bytes(sig_ptr[:64]) if sig_ptr else None

        return {
            "index": index,
            "label": label,
            "threshold": threshold,
            "pattern": pattern,
        }

    def infer(self, data):
        """
        Run inference on input data.

        Args:
            data: bytes or bytearray (must be 64 bytes)

        Returns:
            Result object with match info
        """
        if isinstance(data, (bytes, bytearray)):
            data = bytes(data)
        else:
            raise TypeError("data must be bytes or bytearray")

        if len(data) < 64:
            data = data + b"\x00" * (64 - len(data))
        elif len(data) > 64:
            data = data[:64]

        # Convert to ctypes array
        input_arr = (ctypes.c_uint8 * 64).from_buffer_copy(data)

        result = self._lib.trix_infer(self._chip, input_arr)

        label = result.label.decode("utf-8") if result.label else None
        return Result(result.match, result.distance, result.threshold, label,
                      result.trace_tick_start, result.trace_tick_end)

    def __del__(self):
        if hasattr(self, "_chip") and self._chip and self._lib:
            self._lib.trix_chip_free(self._chip)


def load(path):
    """
    Load a TriX chip from a .trix file.

    Args:
        path: Path to .trix file

    Returns:
        Chip object
    """
    return Chip(path)


def is_available():
    """Check if TriX library is available."""
    return _lib is not None


__all__ = ["Chip", "load", "is_available", "Result"]
__version__ = "1.0.0"
