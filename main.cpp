#include "core/orderbook/OrderBook.h"
#include "core/orderbook/LockFreeOrderBook.h"
#include "core/utils/TimeUtils.h"
#include "strategies/basic/BasicMarketMaker.h"
#include "strategies/config/StrategyConfig.h"
#include "exchange/simulator/ExchangeSimulator.h"
#include "core/persistence/PersistenceManager.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

namespace po = boost::program_options;

// Global flag for signal handling
std::atomic<bool> g_running{true};

// Signal handler function
void signalHandler(int signal) {
    spdlog::warn("Received signal {}, shutting down...", signal);
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    try {
        // Set up signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        // Define and parse command line options
        po::options_description desc("PinnacleMM Options");
        desc.add_options()
            ("help", "Show help message")
            ("symbol", po::value<std::string>()->default_value("BTC-USD"), "Trading symbol")
            ("mode", po::value<std::string>()->default_value("simulation"), "Trading mode (simulation/live)")
            ("config", po::value<std::string>()->default_value("config/default_config.json"), "Configuration file")
            ("logfile", po::value<std::string>()->default_value("pinnaclemm.log"), "Log file")
            ("verbose", po::bool_switch()->default_value(false), "Verbose output")
            ("lock-free", po::bool_switch()->default_value(true), "Use lock-free data structures");
        
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
        
        // Show help message
        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }
        
        // Get command line parameters
        std::string symbol = vm["symbol"].as<std::string>();
        std::string mode = vm["mode"].as<std::string>();
        std::string configFile = vm["config"].as<std::string>();
        std::string logFile = vm["logfile"].as<std::string>();
        bool verbose = vm["verbose"].as<bool>();
        bool useLockFree = vm["lock-free"].as<bool>();
        
        // Initialize logger
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
        auto logger = std::make_shared<spdlog::logger>("pinnaclemm", spdlog::sinks_init_list{console_sink, file_sink});
        if (verbose) {
            logger->set_level(spdlog::level::debug);
        } else {
            logger->set_level(spdlog::level::info);
        }
        spdlog::set_default_logger(logger);
        
        spdlog::info("Starting PinnacleMM for {} in {} mode", symbol, mode);
        spdlog::info("Using lock-free data structures: {}", useLockFree ? "enabled" : "disabled");

        // Initialize persistence
        auto& persistenceManager = pinnacle::persistence::PersistenceManager::getInstance();
        std::string dataDirectory = "data"; // Default data directory from config
        if (!persistenceManager.initialize(dataDirectory)) {
            spdlog::error("Failed to initialize persistence");
            return 1;
        }
        spdlog::info("Persistence initialized with data directory: {}", dataDirectory);
        
        // Create order book
        std::shared_ptr<pinnacle::OrderBook> orderBook;

        if (useLockFree) {
            // Use lock-free order book
            spdlog::info("Using lock-free order book implementation");
            orderBook = std::make_shared<pinnacle::LockFreeOrderBook>(symbol);
        } else {
            // Use default order book
            spdlog::info("Using mutex-based order book implementation");
            orderBook = std::make_shared<pinnacle::OrderBook>(symbol);
        }
        
        // Load strategy configuration
        pinnacle::strategy::StrategyConfig config;
        config.symbol = symbol;
        
        // Initialize strategy
        auto strategy = std::make_shared<pinnacle::strategy::BasicMarketMaker>(symbol, config);
        if (!strategy->initialize(orderBook)) {
            spdlog::error("Failed to initialize strategy");
            return 1;
        }
        
        // Start strategy
        if (!strategy->start()) {
            spdlog::error("Failed to start strategy");
            return 1;
        }
        
        spdlog::info("Strategy started successfully");
        
        // In simulation mode, start the exchange simulator
        std::shared_ptr<pinnacle::exchange::ExchangeSimulator> simulator;
        if (mode == "simulation") {
            // Configure simulator (placeholder - actual implementation would be more complex)
            simulator = std::make_shared<pinnacle::exchange::ExchangeSimulator>(orderBook);
            simulator->start();
            spdlog::info("Exchange simulator started");
        }
        
        // Main application loop
        uint64_t lastStatsTime = 0;
        uint64_t lastCheckpointTime = 0; // Checkpoint persistence timing

        while (g_running.load()) {
            // Current time
            uint64_t currentTime = pinnacle::utils::TimeUtils::getCurrentMillis();
            
            // Print statistics periodically
            if (currentTime - lastStatsTime > 5000) { // Every 5 seconds
                spdlog::info("======================");
                spdlog::info("Current time: {}", pinnacle::utils::TimeUtils::getCurrentISOTimestamp());
                spdlog::info("Order book status:");
                spdlog::info("  Best bid: {}", orderBook->getBestBidPrice());
                spdlog::info("  Best ask: {}", orderBook->getBestAskPrice());
                spdlog::info("  Mid price: {}", orderBook->getMidPrice());
                spdlog::info("  Spread: {}", orderBook->getSpread());
                spdlog::info("  Order count: {}", orderBook->getOrderCount());
                spdlog::info("Strategy status:");
                spdlog::info("{}" , strategy->getStatistics());
                spdlog::info("======================");
                
                lastStatsTime = currentTime;
            }

            // Create checkpoint periodically
            if (currentTime - lastCheckpointTime > 5 * 60 * 1000) { // Every 5 minutes
                spdlog::info("Creating order book checkpoint...");
                orderBook->createCheckpoint();
                lastCheckpointTime = currentTime;
                spdlog::info("Checkpoint created successfully");
            }
            
            // Sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Shutdown
        spdlog::info("Shutting down...");
        
        // Stop strategy
        if (strategy->isRunning()) {
            strategy->stop();
        }
        
        // Stop simulator if running
        if (simulator) {
            simulator->stop();
        }
        
        spdlog::info("Final statistics:");
        spdlog::info("{}", strategy->getStatistics());
        
        spdlog::info("Shutdown complete");
        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }
}