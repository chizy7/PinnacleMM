cmake_minimum_required(VERSION 3.14)
project(
  PinnacleMM
  VERSION 0.2.0
  LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the policy to use the new Boost finding behavior
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -march=native")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Handle Boost with better compatibility
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

# First try the modern CONFIG mode
find_package(Boost 1.72 QUIET CONFIG COMPONENTS system filesystem
                                                program_options)
if(NOT Boost_FOUND)
  # Fallback to legacy FindBoost module
  find_package(Boost 1.72 REQUIRED COMPONENTS system filesystem program_options)
endif()

# Create modern targets if they don't exist (for legacy FindBoost)
if(Boost_FOUND AND NOT TARGET Boost::system)
  add_library(Boost::system INTERFACE IMPORTED)
  target_link_libraries(Boost::system INTERFACE ${Boost_SYSTEM_LIBRARY})
  target_include_directories(Boost::system INTERFACE ${Boost_INCLUDE_DIRS})
endif()

if(Boost_FOUND AND NOT TARGET Boost::filesystem)
  add_library(Boost::filesystem INTERFACE IMPORTED)
  target_link_libraries(Boost::filesystem INTERFACE ${Boost_FILESYSTEM_LIBRARY})
  target_include_directories(Boost::filesystem INTERFACE ${Boost_INCLUDE_DIRS})
endif()

if(Boost_FOUND AND NOT TARGET Boost::program_options)
  add_library(Boost::program_options INTERFACE IMPORTED)
  target_link_libraries(Boost::program_options
                        INTERFACE ${Boost_PROGRAM_OPTIONS_LIBRARY})
  target_include_directories(Boost::program_options
                             INTERFACE ${Boost_INCLUDE_DIRS})
endif()

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Handle spdlog and fmt with fallback to FetchContent
include(FetchContent)

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  message(STATUS "spdlog not found, downloading...")
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0)
  FetchContent_MakeAvailable(spdlog)
endif()

find_package(fmt QUIET)
if(NOT fmt_FOUND)
  message(STATUS "fmt not found, downloading...")
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.1.1)
  FetchContent_MakeAvailable(fmt)
endif()

# Create OpenSSL targets if they don't exist (for compatibility with older
# CMake)
if(OPENSSL_FOUND AND NOT TARGET OpenSSL::SSL)
  add_library(OpenSSL::SSL UNKNOWN IMPORTED)
  set_target_properties(
    OpenSSL::SSL
    PROPERTIES IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
               INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()

if(OPENSSL_FOUND AND NOT TARGET OpenSSL::Crypto)
  add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
  set_target_properties(
    OpenSSL::Crypto
    PROPERTIES IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
               INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
endif()

# WebSocket++ is a header-only library
find_path(WEBSOCKETPP_INCLUDE_DIRS websocketpp/client.hpp
          HINTS /usr/local/include /usr/include /opt/homebrew/include)
if(NOT WEBSOCKETPP_INCLUDE_DIRS)
  message(STATUS "WebSocket++ not found in system, downloading...")
  FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2)
  FetchContent_MakeAvailable(websocketpp)
  set(WEBSOCKETPP_INCLUDE_DIRS ${websocketpp_SOURCE_DIR})
endif()

# hffix is a header-only FIX protocol library
find_path(HFFIX_INCLUDE_DIRS hffix.hpp
          HINTS /usr/local/include /usr/include /opt/homebrew/include
                ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hffix/include)
if(NOT HFFIX_INCLUDE_DIRS)
  message(STATUS "hffix not found in system, will use bundled version")
  set(HFFIX_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hffix/include)
endif()

# Options
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(USE_TBB "Use Intel TBB for parallel algorithms" OFF)
option(USE_DPDK "Use DPDK for kernel bypass networking" OFF)
option(USE_LOCK_FREE "Use lock-free data structures" ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${WEBSOCKETPP_INCLUDE_DIRS}
                    ${HFFIX_INCLUDE_DIRS})

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
    core/utils/TimeUtils.cpp
    core/utils/SecureInput.cpp
    core/utils/InputValidator.cpp
    core/utils/CertificatePinner.cpp
    core/utils/AuditLogger.cpp
    core/utils/RateLimiter.cpp
    core/utils/LockFreeOrderBook.cpp
    core/orderbook/LockFreeOrderBook.cpp
    core/persistence/PersistenceManager.cpp
    core/persistence/journal/Journal.cpp
    core/persistence/journal/JournalEntry.cpp
    core/persistence/snapshot/SnapshotManager.cpp
    core/routing/OrderRouter.cpp)

# Strategy library files
set(STRATEGY_SOURCES strategies/basic/BasicMarketMaker.cpp
                     strategies/config/StrategyConfig.cpp)

# Exchange library files
set(EXCHANGE_SOURCES
    exchange/simulator/ExchangeSimulator.cpp
    exchange/simulator/MarketDataFeed.cpp
    exchange/connector/SecureConfig.cpp
    exchange/connector/WebSocketMarketDataFeed.cpp
    exchange/connector/ExchangeConnectorFactory.cpp
    # FIX Protocol support (core implementation ready, API compatibility
    # pending)
    exchange/fix/FixConnectorFactory.cpp
    # TODO: Re-enable when hffix API compatibility is resolved
    # exchange/fix/FixConnector.cpp
    # exchange/fix/InteractiveBrokersFixConnector.cpp
)

# Create core library
add_library(core STATIC ${CORE_SOURCES})
target_link_libraries(
  core PUBLIC Threads::Threads Boost::system Boost::filesystem OpenSSL::SSL
              OpenSSL::Crypto nlohmann_json::nlohmann_json)

# Create strategy library
add_library(strategy STATIC ${STRATEGY_SOURCES})
target_link_libraries(strategy PUBLIC core Threads::Threads Boost::system
                                      Boost::filesystem)

# Create exchange library
add_library(exchange STATIC ${EXCHANGE_SOURCES})
target_link_libraries(
  exchange
  PUBLIC core
         Threads::Threads
         Boost::system
         Boost::filesystem
         OpenSSL::SSL
         OpenSSL::Crypto
         nlohmann_json::nlohmann_json)

# Compile definitions for real WebSocket implementation
# target_compile_definitions(exchange PRIVATE USE_WEBSOCKET_STUB )

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
target_link_libraries(
  pinnaclemm
  core
  strategy
  exchange
  Threads::Threads
  Boost::system
  Boost::filesystem
  Boost::program_options
  spdlog::spdlog
  fmt::fmt)

# Tests
if(BUILD_TESTS)
  enable_testing()

  # Find Google Test
  find_package(GTest REQUIRED)
  include_directories(${GTEST_INCLUDE_DIRS})

  # Order book tests
  add_executable(orderbook_tests tests/unit/OrderBookTests.cpp)
  target_link_libraries(orderbook_tests core ${GTEST_LIBRARIES}
                        Threads::Threads)
  add_test(NAME OrderBookTests COMMAND orderbook_tests)

  # Lock-free order book tests
  add_executable(lockfree_orderbook_tests tests/unit/LockFreeOrderBookTests.cpp)
  target_link_libraries(lockfree_orderbook_tests core ${GTEST_LIBRARIES}
                        Threads::Threads)
  add_test(NAME LockFreeOrderBookTests COMMAND lockfree_orderbook_tests)

  # Execution tests
  add_executable(execution_tests tests/unit/ExecutionTests.cpp)
  target_link_libraries(execution_tests core ${GTEST_LIBRARIES}
                        Threads::Threads)
  add_test(NAME ExecutionTests COMMAND execution_tests)

  # Strategy tests
  add_executable(strategy_tests tests/unit/StrategyTests.cpp)
  target_link_libraries(strategy_tests core strategy ${GTEST_LIBRARIES}
                        Threads::Threads)
  add_test(NAME StrategyTests COMMAND strategy_tests)

  # FIX Protocol tests - Tests factory patterns, configuration, and core FIX
  # integration
  add_executable(fix_basic_test tests/fix_basic_test.cpp)
  target_link_libraries(fix_basic_test core exchange Threads::Threads)
  add_test(NAME FixBasicTests COMMAND fix_basic_test)

  # Order Router tests - Tests advanced routing strategies and execution logic
  add_executable(routing_test tests/routing_test.cpp)
  target_link_libraries(routing_test core exchange Threads::Threads fmt::fmt)
  add_test(NAME RoutingTests COMMAND routing_test)
endif()

# Benchmarks
if(BUILD_BENCHMARKS)
  # Find Google Benchmark
  find_package(benchmark REQUIRED)

  # Latency benchmarks
  add_executable(latency_benchmark tests/performance/LatencyBenchmark.cpp)
  target_link_libraries(latency_benchmark core strategy exchange
                        benchmark::benchmark Threads::Threads)

  # Throughput benchmarks
  add_executable(throughput_benchmark tests/performance/ThroughputBenchmark.cpp)
  target_link_libraries(throughput_benchmark core strategy exchange
                        benchmark::benchmark Threads::Threads)

  # Order book benchmarks
  add_executable(orderbook_benchmark tests/performance/OrderBookBenchmark.cpp)
  target_link_libraries(orderbook_benchmark core strategy exchange
                        benchmark::benchmark Threads::Threads)

  # Order routing benchmarks
  add_executable(routing_benchmark tests/performance/OrderRoutingBenchmark.cpp)
  target_link_libraries(
    routing_benchmark
    core
    strategy
    exchange
    benchmark::benchmark
    Threads::Threads
    fmt::fmt)
endif()

# Install targets
install(TARGETS pinnaclemm RUNTIME DESTINATION bin)

# Install configuration files
install(FILES config/default_config.json DESTINATION etc/pinnaclemm)

# Print configuration summary
message(STATUS "Configuration Summary:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Build Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  Use TBB: ${USE_TBB}")
message(STATUS "  Use DPDK: ${USE_DPDK}")
message(STATUS "  Use Lock-Free: ${USE_LOCK_FREE}")
message(
  STATUS "  FIX Protocol: Core implementation ready (hffix integration pending)"
)
message(STATUS "  WebSocket Support: Enabled (Boost.Beast)")
message(
  STATUS
    "  Advanced Order Routing: 4 algorithms (BEST_PRICE, TWAP, VWAP, MARKET_IMPACT)"
)
message(STATUS "  Test FIX Integration: Run './fix_basic_test' after build")
message(STATUS "  Test Order Routing: Run './routing_test' after build")
message(
  STATUS
    "  Performance Benchmarks: Run './latency_benchmark', './throughput_benchmark', './orderbook_benchmark', './routing_benchmark'"
)
