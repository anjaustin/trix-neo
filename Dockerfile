# TriX Toolchain Docker Image
#
# Production container for TriX neural inference toolchain
# Includes: parser, code generator, linear forge, CfC forge
#
# Usage:
#   docker build -t trix/toolchain:latest .
#   docker run --rm trix/toolchain:latest trix --help
#   docker run --rm -v $(pwd):/workspace trix/toolchain:latest trix /workspace/spec.trix

# Build stage
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source
COPY . /build/

# Build
RUN cmake . -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc) \
    && make install

# Runtime stage
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libmath \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN useradd -m -s /bin/bash trix

WORKDIR /home/trix

# Copy installed files from builder
COPY --from=builder /usr/local/lib/libtrix_runtime.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libtrix_tools.a /usr/local/lib/
COPY --from=builder /usr/local/bin/trix /usr/local/bin/
COPY --from=builder /usr/local/include /usr/local/include/

# Copy libraries
RUN ldconfig

# Create workspace
RUN mkdir -p /workspace && chown -R trix:trix /workspace

USER trix

# Default command shows help
ENTRYPOINT ["/usr/local/bin/trix"]
CMD ["--help"]