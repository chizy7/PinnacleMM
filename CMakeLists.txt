cmake_minimum_required(VERSION 3.14)
project(PinnacleMM VERSION 0.2.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -march=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(Boost 1.72 REQUIRED COMPONENTS system filesystem program_options)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# WebSocket++ is a header-only library
find_path(WEBSOCKETPP_INCLUDE_DIRS websocketpp/client.hpp
    HINTS 
    /usr/local/include
    /usr/include
    /opt/homebrew/include
)
if(NOT WEBSOCKETPP_INCLUDE_DIRS)
    message(FATAL_ERROR "WebSocket++ not found")
endif()

# Options
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(USE_TBB "Use Intel TBB for parallel algorithms" OFF)
option(USE_DPDK "Use DPDK for kernel bypass networking" OFF)
option(USE_LOCK_FREE "Use lock-free data structures" ON)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${spdlog_INCLUDE_DIRS}
    ${fmt_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${WEBSOCKETPP_INCLUDE_DIRS}
)

# Add TBB if enabled
if(USE_TBB)
    find_package(TBB REQUIRED)
    add_definitions(-DUSE_TBB)
    include_directories(${TBB_INCLUDE_DIRS})
endif()

# Add DPDK if enabled
if(USE_DPDK)
    find_package(DPDK REQUIRED)
    add_definitions(-DUSE_DPDK)
    include_directories(${DPDK_INCLUDE_DIRS})
endif()

# Use lock-free data structures if enabled
if(USE_LOCK_FREE)
    add_definitions(-DUSE_LOCK_FREE)
endif()

# Core library files
set(CORE_SOURCES
    core/orderbook/Order.cpp
    core/orderbook/OrderBook.cpp
    core/execution/ExecutionEngine.cpp
    core/risk/RiskManager.cpp
    core/utils/TimeUtils.cpp
    core/utils/Config.cpp
    core/utils/LockFreeOrderBook.cpp 
    core/orderbook/LockFreeOrderBook.cpp 
    core/persistence/PersistenceManager.cpp
    core/persistence/journal/Journal.cpp
    core/persistence/journal/JournalEntry.cpp
    core/persistence/snapshot/SnapshotManager.cpp
)

# Strategy library files
set(STRATEGY_SOURCES
    strategies/basic/BasicMarketMaker.cpp
    strategies/config/StrategyConfig.cpp
)

# Exchange library files
set(EXCHANGE_SOURCES
    exchange/simulator/ExchangeSimulator.cpp
    exchange/simulator/MarketDataFeed.cpp
    exchange/connector/SecureConfig.cpp
    exchange/connector/WebSocketMarketDataFeedStub.cpp
    exchange/connector/ExchangeConnectorFactory.cpp
)

# Create core library
add_library(core STATIC ${CORE_SOURCES})
target_link_libraries(core PUBLIC
    Threads::Threads
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Create strategy library
add_library(strategy STATIC ${STRATEGY_SOURCES})
target_link_libraries(strategy PUBLIC
    core
    Threads::Threads
    ${Boost_LIBRARIES}
)

# Create exchange library
add_library(exchange STATIC ${EXCHANGE_SOURCES})
target_link_libraries(exchange PUBLIC
    core
    Threads::Threads
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES} 
    nlohmann_json::nlohmann_json
)

# Compile definitions for the stub implementation
target_compile_definitions(exchange PRIVATE 
    USE_WEBSOCKET_STUB
)

# Add TBB linking if enabled
if(USE_TBB)
    target_link_libraries(core PUBLIC ${TBB_LIBRARIES})
endif()

# Add DPDK linking if enabled
if(USE_DPDK)
    target_link_libraries(core PUBLIC ${DPDK_LIBRARIES})
endif()

# Main executable
add_executable(pinnaclemm main.cpp)
target_link_libraries(pinnaclemm
    core
    strategy
    exchange
    Threads::Threads
    ${Boost_LIBRARIES}
    spdlog::spdlog
    fmt::fmt
)

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Find Google Test
    find_package(GTest REQUIRED)
    include_directories(${GTEST_INCLUDE_DIRS})
    
    # Order book tests
    add_executable(orderbook_tests tests/unit/OrderBookTests.cpp)
    target_link_libraries(orderbook_tests
        core
        ${GTEST_LIBRARIES}
        Threads::Threads
    )
    add_test(NAME OrderBookTests COMMAND orderbook_tests)

    # Lock-free order book tests
    add_executable(lockfree_orderbook_tests tests/unit/LockFreeOrderBookTests.cpp)
    target_link_libraries(lockfree_orderbook_tests
        core
        ${GTEST_LIBRARIES}
        Threads::Threads
    )
    add_test(NAME LockFreeOrderBookTests COMMAND lockfree_orderbook_tests)
    
    # Execution tests
    add_executable(execution_tests tests/unit/ExecutionTests.cpp)
    target_link_libraries(execution_tests
        core
        ${GTEST_LIBRARIES}
        Threads::Threads
    )
    add_test(NAME ExecutionTests COMMAND execution_tests)
    
    # Strategy tests
    add_executable(strategy_tests tests/unit/StrategyTests.cpp)
    target_link_libraries(strategy_tests
        core
        strategy
        ${GTEST_LIBRARIES}
        Threads::Threads
    )
    add_test(NAME StrategyTests COMMAND strategy_tests)
endif()

# Benchmarks
if(BUILD_BENCHMARKS)
    # Find Google Benchmark
    find_package(benchmark REQUIRED)
    
    # Latency benchmarks
    add_executable(latency_benchmark tests/performance/LatencyBenchmark.cpp)
    target_link_libraries(latency_benchmark
        core
        strategy
        exchange
        benchmark::benchmark
        Threads::Threads
    )
    
    # Throughput benchmarks
    add_executable(throughput_benchmark tests/performance/ThroughputBenchmark.cpp)
    target_link_libraries(throughput_benchmark
        core
        strategy
        exchange
        benchmark::benchmark
        Threads::Threads
    )

    # Order book benchmarks
    add_executable(orderbook_benchmark tests/performance/OrderBookBenchmark.cpp)
    target_link_libraries(orderbook_benchmark
        core
        strategy
        exchange
        benchmark::benchmark
        Threads::Threads
    )
endif()

# Install targets
install(TARGETS pinnaclemm
    RUNTIME DESTINATION bin
)

# Install configuration files
install(FILES
    config/default_config.json
    DESTINATION etc/pinnaclemm
)

# Print configuration summary
message(STATUS "Configuration Summary:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  Use TBB: ${USE_TBB}")
message(STATUS "  Use DPDK: ${USE_DPDK}")
message(STATUS "  Use Lock-Free: ${USE_LOCK_FREE}")