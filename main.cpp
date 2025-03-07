#include "core/orderbook/OrderBook.h"
#include "core/utils/TimeUtils.h"
#include "strategies/basic/BasicMarketMaker.h"
#include "strategies/config/StrategyConfig.h"
#include "exchange/simulator/ExchangeSimulator.h"

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
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
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
            ("verbose", po::bool_switch()->default_value(false), "Verbose output");
        
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
        bool verbose = vm["verbose"].as<bool>();
        
        std::cout << "Starting PinnacleMM for " << symbol << " in " << mode << " mode" << std::endl;
        
        // Create order book
        auto orderBook = std::make_shared<pinnacle::OrderBook>(symbol);
        
        // Load strategy configuration
        pinnacle::strategy::StrategyConfig config;
        config.symbol = symbol;
        
        // Initialize strategy
        auto strategy = std::make_shared<pinnacle::strategy::BasicMarketMaker>(symbol, config);
        if (!strategy->initialize(orderBook)) {
            std::cerr << "Failed to initialize strategy" << std::endl;
            return 1;
        }
        
        // Start strategy
        if (!strategy->start()) {
            std::cerr << "Failed to start strategy" << std::endl;
            return 1;
        }
        
        std::cout << "Strategy started successfully" << std::endl;
        
        // In simulation mode, start the exchange simulator
        std::shared_ptr<pinnacle::exchange::ExchangeSimulator> simulator;
        if (mode == "simulation") {
            // Configure simulator (placeholder - actual implementation would be more complex)
            simulator = std::make_shared<pinnacle::exchange::ExchangeSimulator>(orderBook);
            simulator->start();
            std::cout << "Exchange simulator started" << std::endl;
        }
        
        // Main application loop
        uint64_t lastStatsTime = 0;
        while (g_running.load()) {
            // Current time
            uint64_t currentTime = pinnacle::utils::TimeUtils::getCurrentMillis();
            
            // Print statistics periodically
            if (currentTime - lastStatsTime > 5000) { // Every 5 seconds
                std::cout << "======================" << std::endl;
                std::cout << "Current time: " << pinnacle::utils::TimeUtils::getCurrentISOTimestamp() << std::endl;
                std::cout << "Order book status:" << std::endl;
                std::cout << "  Best bid: " << orderBook->getBestBidPrice() << std::endl;
                std::cout << "  Best ask: " << orderBook->getBestAskPrice() << std::endl;
                std::cout << "  Mid price: " << orderBook->getMidPrice() << std::endl;
                std::cout << "  Spread: " << orderBook->getSpread() << std::endl;
                std::cout << "  Order count: " << orderBook->getOrderCount() << std::endl;
                std::cout << "Strategy status:" << std::endl;
                std::cout << strategy->getStatistics() << std::endl;
                std::cout << "======================" << std::endl;
                
                lastStatsTime = currentTime;
            }
            
            // Sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Shutdown
        std::cout << "Shutting down..." << std::endl;
        
        // Stop strategy
        if (strategy->isRunning()) {
            strategy->stop();
        }
        
        // Stop simulator if running
        if (simulator) {
            simulator->stop();
        }
        
        std::cout << "Final statistics:" << std::endl;
        std::cout << strategy->getStatistics() << std::endl;
        
        std::cout << "Shutdown complete" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}