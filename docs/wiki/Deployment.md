# Deployment Guide

Complete guide for deploying TriX in production environments.

## Table of Contents
- [Docker Deployment](#docker-deployment)
- [Embedded Systems](#embedded-systems)
- [Cloud Deployment](#cloud-deployment)
- [CI/CD Integration](#cicd-integration)
- [Monitoring](#monitoring)

---

## Docker Deployment

### Quick Start

```bash
# Build the image
docker build -t trix/toolchain:latest .

# Run the toolchain
docker run --rm trix/toolchain:latest --help

# Run with volume mount
docker run --rm -v $(pwd):/workspace trix/toolchain:latest gesture.trix
```

### Multi-stage Build

The Dockerfile uses multi-stage builds for minimal image size:

```dockerfile
# Build stage
FROM ubuntu:22.04 AS builder
# ... compile TriX ...

# Runtime stage  
FROM ubuntu:22.04 AS runtime
# Only runtime libraries, ~50MB
COPY --from=builder /usr/local/bin/trix /usr/local/bin/
COPY --from=builder /usr/local/lib/libtrix_runtime.so* /usr/local/lib/
```

### Docker Compose

```yaml
version: '3.8'
services:
  trix:
    image: trix/toolchain:latest
    volumes:
      - ./models:/models
      - ./output:/output
    environment:
      - TRIX_LOG_LEVEL=info
    command: ["--input", "/models/gesture.trix"]
```

---

## Embedded Systems

### ARM32 (Raspberry Pi)

```bash
# Cross-compile for ARM32
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake \
  -DCMAKE_BUILD_TYPE=Release

# Deploy
scp build/trix pi@raspberrypi:/usr/local/bin/
scp build/libtrix_runtime.so pi@raspberrypi:/usr/local/lib/
```

### ARM64 (NVIDIA Jetson)

```bash
# Jetson has NEON, compile natively
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-march=armv8.2-a+crypto"

# Deploy
sudo cp build/trix /usr/local/bin/
sudo cp build/libtrix_runtime.so* /usr/local/lib/
sudo ldconfig
```

### Bare Metal

For safety-critical embedded systems:

```c
// No dynamic memory allocation
#define TRIX_NO_MALLOC

// Include only what you need
#include <trixc/runtime.h>
#include <trixc/memory.h>
```

---

## Cloud Deployment

### AWS Lambda

```dockerfile
FROM amazonlinux:2023
RUN yum install -y cmake gcc
COPY . /build
RUN cd /build && cmake . && make
CMD ["/build/trix"]
```

### Kubernetes

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: trix-inference
spec:
  containers:
  - name: trix
    image: trix/toolchain:latest
    resources:
      limits:
        memory: "128Mi"
        cpu: "500m"
```

---

## CI/CD Integration

### GitHub Actions

```yaml
- name: Build TriX
  run: |
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make

- name: Run Tests
  run: ctest --output-on-failure

- name: Build Docker
  uses: docker/build-push-action@v5
  with:
    push: false
    tags: trix/toolchain:test
```

### GitLab CI

```yaml
build:
  script:
    - mkdir build && cd build
    - cmake .. -DCMAKE_BUILD_TYPE=Release
    - make
    - ctest --output-on-failure
```

---

## Monitoring

### Prometheus Metrics

```c
#include <trixc/metrics.h>

// Create metrics
int inference_count = trix_counter_create("trix_inferences_total", "Total inferences");
int active_chips = trix_gauge_create("trix_active_chips", "Active chip instances");

// In inference loop
trix_counter_inc(inference_count);

// Export for Prometheus
char buffer[8192];
trix_metrics_export_prometheus(buffer, sizeof(buffer));
// Serve at /metrics
```

### Health Checks

```c
// Simple health check
int health_check(void) {
    return TRIX_OK;  // Always healthy if we get here
}
```

---

## Security Considerations

### Secure Build

```bash
# Enable AddressSanitizer to catch memory issues
cmake .. -DENABLE_SANITIZERS=ON

# Enable runtime bounds checking
cmake .. -DTRIX_STRICT=ON
```

### Running as Non-root (Docker)

```dockerfile
# Add non-root user
RUN useradd -m trix
USER trix
```

---

## Performance Tuning

### Compile Optimizations

| Flag | Effect |
|------|--------|
| `-O3` | Maximum optimization |
| `-march=native` | CPU-specific optimizations |
| `-ffast-math` | Fast math (not for TriX) |
| `-flto` | Link-time optimization |

### Runtime Settings

```c
// Set log level
trix_log_set_level(TRIX_LOG_WARN);  // Reduce logging overhead

// Pre-allocate chip (avoid first-call latency)
trix_chip_t* chip = trix_load("model.trix", NULL);
```

---

## Troubleshooting

### "libtrix_runtime.so not found"

```bash
# Add to library path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Or add to ldconfig
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/trix.conf
sudo ldconfig
```

### "arm_neon.h not found"

Install NEON headers:
```bash
# Ubuntu
sudo apt-get install gcc-arm-none-eabi
```