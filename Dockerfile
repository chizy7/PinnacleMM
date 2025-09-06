FROM ubuntu:22.04 AS builder

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    libboost-all-dev \
    libgtest-dev \
    git \
    libspdlog-dev \
    libfmt-dev \
    libssl-dev \
    nlohmann-json3-dev \
    libwebsocketpp-dev \
    && rm -rf /var/lib/apt/lists/*

# Build Google Test
WORKDIR /usr/src/googletest
RUN cmake . && make && cp lib/libgtest*.a /usr/lib/

# Install Google Benchmark
WORKDIR /tmp
RUN git clone https://github.com/google/benchmark.git && \
    cd benchmark && \
    cmake -E make_directory build && \
    cmake -E chdir build cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build build --target install

# Set up working directory
WORKDIR /app

# Copy source code
COPY . .

# Build the project with tests disabled
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF && \
    make -j$(nproc)

# Create runtime image
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
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