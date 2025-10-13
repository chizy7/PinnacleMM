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

# Address Sanitizer option for development/testing
option(ENABLE_SANITIZERS "Enable Address Sanitizer for development and testing"
       OFF)

# Set compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -march=native")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")

  # Add sanitizers for development builds
  if(ENABLE_SANITIZERS)
    message(STATUS "Address Sanitizer enabled for development/testing")
    set(SANITIZER_FLAGS
        "-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG
        "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
  endif()
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
  # Set policy to handle older CMake versions in WebSocket++
  set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
  FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2
    GIT_SHALLOW TRUE
    CONFIGURE_COMMAND "" BUILD_COMMAND "")
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
option(BUILD_VISUALIZATION "Build visualization server" ON)
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
    core/utils/JsonLogger.cpp
    core/utils/RateLimiter.cpp
    core/utils/LockFreeOrderBook.cpp
    core/orderbook/LockFreeOrderBook.cpp
    core/persistence/PersistenceManager.cpp
    core/persistence/journal/Journal.cpp
    core/persistence/journal/JournalEntry.cpp
    core/persistence/snapshot/SnapshotManager.cpp
    core/routing/OrderRouter.cpp)

# Strategy library files
set(STRATEGY_SOURCES
    strategies/basic/BasicMarketMaker.cpp
    strategies/basic/MLEnhancedMarketMaker.cpp
    strategies/ml/MLSpreadOptimizer.cpp
    strategies/analytics/OrderBookAnalyzer.cpp
    strategies/analytics/MarketImpactPredictor.cpp
    strategies/analytics/MarketRegimeDetector.cpp
    strategies/rl/RLParameterAdapter.cpp
    strategies/backtesting/BacktestEngine.cpp
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
  core
  PUBLIC Threads::Threads
         Boost::system
         Boost::filesystem
         OpenSSL::SSL
         OpenSSL::Crypto
         nlohmann_json::nlohmann_json
         spdlog::spdlog
         fmt::fmt)

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

# Create visualization library
if(BUILD_VISUALIZATION)
  set(VISUALIZATION_SOURCES visualization/WebServer.cpp)

  add_library(visualization STATIC ${VISUALIZATION_SOURCES})
  target_link_libraries(
    visualization
    PUBLIC core
           strategy
           Threads::Threads
           Boost::system
           Boost::filesystem
           nlohmann_json::nlohmann_json
           spdlog::spdlog
           fmt::fmt)

  target_include_directories(visualization PRIVATE ${WEBSOCKETPP_INCLUDE_DIRS})
  target_compile_definitions(visualization PRIVATE ASIO_STANDALONE
                                                   BUILD_VISUALIZATION)
endif()

# Main executable
add_executable(pinnaclemm main.cpp)

# Add BUILD_VISUALIZATION definition to main executable if visualization is
# enabled
if(BUILD_VISUALIZATION)
  target_compile_definitions(pinnaclemm PRIVATE BUILD_VISUALIZATION)
endif()
set(PINNACLEMM_LIBS
    core
    strategy
    exchange
    Threads::Threads
    Boost::system
    Boost::filesystem
    Boost::program_options
    spdlog::spdlog
    fmt::fmt)

if(BUILD_VISUALIZATION)
  list(APPEND PINNACLEMM_LIBS visualization)
endif()

target_link_libraries(pinnaclemm ${PINNACLEMM_LIBS})

# Tests
if(BUILD_TESTS)
  enable_testing()

  # Find Google Test with fallback to FetchContent
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    message(STATUS "GTest not found, downloading...")
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
      GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Create aliases for consistent naming
    if(TARGET gtest AND NOT TARGET GTest::gtest)
      add_library(GTest::gtest ALIAS gtest)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
  endif()

  # Order book tests
  add_executable(orderbook_tests tests/unit/OrderBookTests.cpp)
  target_link_libraries(orderbook_tests core GTest::gtest_main GTest::gtest
                        Threads::Threads)
  add_test(NAME OrderBookTests COMMAND orderbook_tests)

  # Lock-free order book tests
  add_executable(lockfree_orderbook_tests tests/unit/LockFreeOrderBookTests.cpp)
  target_link_libraries(lockfree_orderbook_tests core GTest::gtest_main
                        GTest::gtest Threads::Threads)
  add_test(NAME LockFreeOrderBookTests COMMAND lockfree_orderbook_tests)

  # Execution tests
  add_executable(execution_tests tests/unit/ExecutionTests.cpp)
  target_link_libraries(execution_tests core GTest::gtest_main GTest::gtest
                        Threads::Threads)
  add_test(NAME ExecutionTests COMMAND execution_tests)

  # Strategy tests
  add_executable(strategy_tests tests/unit/StrategyTests.cpp)
  target_link_libraries(strategy_tests core strategy GTest::gtest_main
                        GTest::gtest Threads::Threads)
  add_test(NAME StrategyTests COMMAND strategy_tests)

  # ML Spread Optimizer tests
  add_executable(ml_spread_optimizer_tests
                 tests/unit/MLSpreadOptimizerTests.cpp)
  target_link_libraries(ml_spread_optimizer_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME MLSpreadOptimizerTests COMMAND ml_spread_optimizer_tests)

  # ML Enhanced Market Maker tests
  add_executable(ml_enhanced_market_maker_tests
                 tests/unit/MLEnhancedMarketMakerTests.cpp)
  target_link_libraries(ml_enhanced_market_maker_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME MLEnhancedMarketMakerTests
           COMMAND ml_enhanced_market_maker_tests)

  # Order Book Analyzer tests
  add_executable(orderbook_analyzer_tests tests/unit/OrderBookAnalyzerTests.cpp)
  target_link_libraries(orderbook_analyzer_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME OrderBookAnalyzerTests COMMAND orderbook_analyzer_tests)

  # Market Impact Predictor tests
  add_executable(market_impact_predictor_tests
                 tests/unit/MarketImpactPredictorTests.cpp)
  target_link_libraries(market_impact_predictor_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME MarketImpactPredictorTests
           COMMAND market_impact_predictor_tests)

  # RL Parameter Adapter tests
  add_executable(rl_parameter_adapter_tests
                 tests/unit/RLParameterAdapterTests.cpp)
  target_link_libraries(rl_parameter_adapter_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME RLParameterAdapterTests COMMAND rl_parameter_adapter_tests)

  # Market Regime Detector tests
  add_executable(market_regime_detector_tests
                 tests/unit/MarketRegimeDetectorTests.cpp)
  target_link_libraries(market_regime_detector_tests core strategy
                        GTest::gtest_main GTest::gtest Threads::Threads)
  add_test(NAME MarketRegimeDetectorTests COMMAND market_regime_detector_tests)

  # Backtest Engine tests
  add_executable(backtest_engine_tests tests/unit/BacktestEngineTests.cpp)
  target_link_libraries(backtest_engine_tests core strategy GTest::gtest_main
                        GTest::gtest Threads::Threads)
  add_test(NAME BacktestEngineTests COMMAND backtest_engine_tests)

  # Certificate Pinner tests - Tests certificate pinning and validation
  add_executable(certificate_pinner_tests tests/unit/CertificatePinnerTest.cpp)
  target_link_libraries(
    certificate_pinner_tests
    core
    GTest::gtest_main
    GTest::gtest
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto)
  add_test(NAME CertificatePinnerTests COMMAND certificate_pinner_tests)

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
  # Find Google Benchmark with fallback to FetchContent
  find_package(benchmark QUIET)
  if(NOT benchmark_FOUND)
    message(STATUS "benchmark not found, downloading...")
    FetchContent_Declare(
      benchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.8.3
      GIT_SHALLOW TRUE)
    set(BENCHMARK_ENABLE_TESTING
        OFF
        CACHE BOOL "Enable testing of the benchmark library.")
    set(BENCHMARK_ENABLE_GTEST_TESTS
        OFF
        CACHE BOOL "Enable building the unit tests which depend on gtest")
    FetchContent_MakeAvailable(benchmark)
  endif()

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

  # ML Spread Optimization benchmarks
  add_executable(ml_spread_optimization_benchmark
                 tests/performance/MLSpreadOptimizationBenchmark.cpp)
  target_link_libraries(ml_spread_optimization_benchmark core strategy
                        benchmark::benchmark Threads::Threads)

  # Market Impact Prediction benchmarks
  add_executable(market_impact_prediction_benchmark
                 tests/performance/MarketImpactPredictionBenchmark.cpp)
  target_link_libraries(market_impact_prediction_benchmark core strategy
                        benchmark::benchmark Threads::Threads)

  # Backtesting benchmarks
  add_executable(backtesting_benchmark
                 tests/performance/BacktestingBenchmark.cpp)
  target_link_libraries(backtesting_benchmark core strategy
                        benchmark::benchmark Threads::Threads)
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
