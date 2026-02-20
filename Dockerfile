FROM ubuntu:22.04 AS builder

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    g++-11 \
    gcc-11 \
    libboost-all-dev \
    libgtest-dev \
    git \
    ca-certificates \
    libspdlog-dev \
    libfmt-dev \
    libssl-dev \
    libssl3 \
    libcrypto++8 \
    openssl \
    nlohmann-json3-dev \
    libwebsocketpp-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/* \
    && ldconfig

# Set GCC 11 as default compiler for C++20 support
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

# Set environment variables for better compatibility
ENV CMAKE_CXX_STANDARD=20
ENV CMAKE_CXX_STANDARD_REQUIRED=ON
ENV CC=/usr/bin/gcc-11
ENV CXX=/usr/bin/g++-11
ENV OPENSSL_ROOT_DIR=/usr
ENV OPENSSL_INCLUDE_DIR=/usr/include
ENV OPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so
ENV OPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so
ENV PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig

# Ensure OpenSSL libraries are properly linked and architecture detection
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN ldconfig && \
    pkg-config --exists openssl || echo "OpenSSL pkg-config not found" && \
    find /usr -name "libssl*" -type f 2>/dev/null | head -10 && \
    find /usr -name "libcrypto*" -type f 2>/dev/null | head -10 && \
    echo "Detected architecture: $(uname -m)" && \
    echo "Available library directories:" && \
    ls -la /usr/lib/*/libssl* /usr/lib/*/libcrypto* 2>/dev/null || true && \
    if [ "$(uname -m)" = "aarch64" ]; then \
        export OPENSSL_CRYPTO_LIBRARY=/usr/lib/aarch64-linux-gnu/libcrypto.so; \
        export OPENSSL_SSL_LIBRARY=/usr/lib/aarch64-linux-gnu/libssl.so; \
    fi

# Build Google Test
WORKDIR /usr/src/googletest
RUN cmake . && make && cp lib/libgtest*.a /usr/lib/

# Install Google Benchmark
WORKDIR /tmp
RUN git config --global http.sslverify false && \
    git clone https://github.com/google/benchmark.git && \
    git config --global --unset http.sslverify
WORKDIR /tmp/benchmark
RUN cmake -E make_directory build && \
    cmake -E chdir build cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build build --target install

# Set up working directory
WORKDIR /app

# Copy source code
COPY . .

# Build the project with tests disabled
RUN rm -rf build && mkdir -p build
WORKDIR /app/build

# Clear any cached CMake files and detect architecture for OpenSSL paths
RUN rm -f CMakeCache.txt && \
    ARCH=$(uname -m) && \
    echo "Building for architecture: $ARCH" && \
    if [ "$ARCH" = "aarch64" ]; then \
        CRYPTO_LIB=/usr/lib/aarch64-linux-gnu/libcrypto.so; \
        SSL_LIB=/usr/lib/aarch64-linux-gnu/libssl.so; \
    else \
        CRYPTO_LIB=/usr/lib/x86_64-linux-gnu/libcrypto.so; \
        SSL_LIB=/usr/lib/x86_64-linux-gnu/libssl.so; \
    fi && \
    echo "Using OpenSSL libraries: $CRYPTO_LIB, $SSL_LIB" && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_BENCHMARKS=OFF \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
        -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare" \
        -DOPENSSL_ROOT_DIR=/usr \
        -DOPENSSL_INCLUDE_DIR=/usr/include \
        -DOPENSSL_CRYPTO_LIBRARY="$CRYPTO_LIB" \
        -DOPENSSL_SSL_LIBRARY="$SSL_LIB" \
        -DPkgConfig_EXECUTABLE=/usr/bin/pkg-config \
        -DCMAKE_VERBOSE_MAKEFILE=ON && \
    make -j"$(nproc)"

# Create runtime image
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.74.0 \
    libboost-filesystem1.74.0 \
    libboost-program-options1.74.0 \
    libspdlog1 \
    libfmt8 \
    libssl3 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy executable and config files from builder
COPY --from=builder /app/build/pinnaclemm /app/
COPY --from=builder /app/config/ /app/config/

# Create logs and data directories
RUN mkdir -p /app/logs /app/data /app/data/backups

# Set environment variables
ENV LD_LIBRARY_PATH=/usr/local/lib

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=30s --retries=3 \
  CMD curl -f http://localhost:8081/api/health || exit 1

# Default command
ENTRYPOINT ["/app/pinnaclemm"]
CMD ["--mode", "simulation", "--symbol", "BTC-USD", "--verbose"]
