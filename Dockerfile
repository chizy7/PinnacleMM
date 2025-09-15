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
    nlohmann-json3-dev \
    libwebsocketpp-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Set GCC 11 as default compiler for C++20 support
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

# Set environment variables for better compatibility
ENV CMAKE_CXX_STANDARD=20
ENV CMAKE_CXX_STANDARD_REQUIRED=ON
ENV CC=/usr/bin/gcc-11
ENV CXX=/usr/bin/g++-11
ENV OPENSSL_ROOT_DIR=/usr
ENV PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig

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
RUN cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
    -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
    -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare" && \
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
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy executable and config files from builder
COPY --from=builder /app/build/pinnaclemm /app/
COPY --from=builder /app/config/ /app/config/

# Set environment variables
ENV LD_LIBRARY_PATH=/usr/local/lib

# Default command
ENTRYPOINT ["/app/pinnaclemm"]
CMD ["--mode", "simulation", "--symbol", "BTC-USD", "--verbose"]
