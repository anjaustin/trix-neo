# CUDA Architecture - The EntroMorphic Cortex

GPU-accelerated visualization and simulation of 16 million liquid neurons.

## Overview

The CUDA subsystem implements a 256^3 voxel grid where each voxel contains a CfC (Closed-form Continuous-time) neuron. This enables real-time simulation and visualization of emergent neural dynamics.

## Files

| File | Purpose | Performance |
|------|---------|-------------|
| `cortex.cu` | 16M neuron physics simulation | ~700 GOP/s |
| `visor.cu` | Volumetric raymarching renderer | 60 fps @ 1024x1024 |
| `visor_bridge.cu` | CUDA-OpenGL interop utilities | Zero-copy |
| `visor_reflex.cu` | Cybernetic immune system | Real-time reflexes |
| `visor_fallback.cu` | CPU fallback renderer | ~5 fps |

## Architecture

### The Cortex (cortex.cu)

A 256^3 grid of V3 soft chips running in parallel:

```
┌─────────────────────────────────────────────────────────────┐
│                    256 × 256 × 256                          │
│                   16,777,216 voxels                         │
│                                                             │
│  Each voxel:                                                │
│    - 19 state floats (CfC hidden state)                     │
│    - 1 output float (signal strength)                       │
│    - 6-neighbor connectivity (von Neumann stencil)          │
│                                                             │
│  Memory: ~1.2 GB VRAM (double-buffered)                     │
└─────────────────────────────────────────────────────────────┘
```

**Key Design Decisions:**

1. **Structure of Arrays (SoA)** - Instead of storing each voxel as a struct, we store 19 separate float arrays. This enables coalesced memory access where adjacent threads read adjacent memory addresses.

2. **Double Buffering** - Ping-pong buffers prevent race conditions during parallel updates. Each kernel reads from buffer A, writes to buffer B, then swap.

3. **Von Neumann Stencil** - Each voxel reads signals from its 6 orthogonal neighbors (±x, ±y, ±z). This creates wave propagation behavior.

### The Visor (visor.cu)

Volumetric raymarching renderer:

```
"The MRI of an Artificial Mind"

Camera → Ray → March through volume → Accumulate light → Pixel
         │
         └─ At each step:
            1. Sample density at position
            2. Apply transfer function (density → color)
            3. Front-to-back alpha blending
            4. Early exit when alpha saturates
```

**Transfer Function:**

| Density | Color | Meaning |
|---------|-------|---------|
| < 0.01 | Transparent | Quiescent |
| 0.01-0.1 | Blue mist | Baseline activity |
| 0.1-1.0 | Orange/Purple | Active processing |
| > 1.0 | White plasma | Anomaly detected |

**Controls:**

| Key | Action |
|-----|--------|
| ESC | Quit |
| SPACE | Inject stimulus |
| W/S | Zoom in/out |
| A/D | Rotate camera |

### The Reflex (visor_reflex.cu)

Cybernetic immune system that protects the host machine:

```
Monitor total energy → Detect panic → Kill aggressor process

When Σ(voxel_energy) > PANIC_THRESHOLD:
  1. Identify top CPU consumer (parse /proc)
  2. Send SIGSTOP to pause it
  3. Enter cooldown period
  4. Resume when energy dissipates
```

This is the machine that protects itself.

## Memory Layout

### SoA Layout (GPU-optimal)

```c
// BAD: Array of Structures (AoS)
struct Voxel { float h[19]; float out; };
Voxel grid[16M];  // Strided access, poor coalescing

// GOOD: Structure of Arrays (SoA)
float h0[16M], h1[16M], ..., h18[16M], out[16M];
// Adjacent threads access adjacent memory
```

### Indexing

```c
// 3D to 1D index (Z-major order)
__device__ int idx(int x, int y, int z) {
    return z * DIM * DIM + y * DIM + x;
}

// Thread block config: 8×8×8 = 512 threads
dim3 block(8, 8, 8);
dim3 grid(DIM/8, DIM/8, DIM/8);
```

## Compilation

### NVIDIA GPU (RTX 4090)

```bash
nvcc -O3 -arch=sm_89 cortex.cu -o cortex
nvcc -O3 -arch=sm_89 visor.cu -o visor -lglfw -lGL -lGLEW
```

### Newer Architectures

```bash
nvcc -O3 -arch=native cortex.cu -o cortex
```

### With OpenGL Interop

```bash
nvcc -O3 -arch=sm_89 visor.cu -o visor \
    -lglfw -lGL -lGLEW -lX11
```

## Performance

### Cortex Benchmark (16M voxels)

| Metric | Value |
|--------|-------|
| Voxels | 16,777,216 |
| State per voxel | 20 floats (80 bytes) |
| Total state | ~1.2 GB |
| Update rate | ~700 GOP/s |
| Timestep | 240 Hz physics |

### Visor Benchmark

| Metric | Value |
|--------|-------|
| Resolution | 1024 × 1024 |
| Ray steps | 300 max |
| Frame rate | 60 fps |
| Physics/frame | 4 steps |

## Integration with Zor

The CUDA cortex uses the same CfC equations as the CPU version:

```c
// Same math, parallel execution
alpha_step(input, h_in, h_out)  // CPU: zor/include/alpha_device.cuh
liquid_update<<<grid, block>>>() // GPU: cortex.cu
```

The frozen chip weights are embedded in `alpha_device.cuh` and shared between CPU and GPU implementations.

## Dependencies

- CUDA Toolkit 11.0+
- GLFW3 (window management)
- GLEW (OpenGL extensions)
- OpenGL 3.3+ (rendering)

### Ubuntu/Debian

```bash
sudo apt install libglfw3-dev libglew-dev
```

### macOS (CPU fallback only)

CUDA requires NVIDIA GPU. Use `visor_fallback.cu` for CPU-based rendering on macOS.

## Related Documentation

- [zor/docs/CFC_ENTROMORPH.md](CFC_ENTROMORPH.md) - CfC theory
- [zor/docs/ZIT_DETECTION.md](ZIT_DETECTION.md) - Anomaly detection
- [zor/foundry/README.md](../foundry/README.md) - Evolution/Genesis
