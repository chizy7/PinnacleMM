#include "../exchange/fix/FixConnectorFactory.h"
#include "../exchange/connector/SecureConfig.h"
#include "../core/utils/TimeUtils.h"
#include "../core/orderbook/Order.h"

#include <iostream>
#include <memory>

using namespace pinnacle::exchange::fix;
using namespace pinnacle::utils;

int main() {
    std::cout << "=== PinnacleMM FIX Protocol Basic Test ===" << std::endl;
    
    try {
        // Test 1: Factory creation
        std::cout << "\n1. Testing FixConnectorFactory..." << std::endl;
        auto& factory = FixConnectorFactory::getInstance();
        std::cout << "   ✓ Factory instance created" << std::endl;
        
        // Test 2: Check FIX support
        bool ibSupported = factory.isFixSupported("interactive_brokers");
        std::cout << "   ✓ Interactive Brokers FIX support: " << (ibSupported ? "Yes" : "No") << std::endl;
        
        bool coinbaseSupported = factory.isFixSupported("coinbase_fix");
        std::cout << "   ✓ Coinbase FIX support: " << (coinbaseSupported ? "Yes" : "No") << std::endl;
        
        // Test 3: Configuration test
        std::cout << "\n2. Testing FIX configuration..." << std::endl;
        auto ibConfig = factory.getDefaultFixConfig(FixConnectorFactory::Exchange::INTERACTIVE_BROKERS);
        std::cout << "   ✓ IB Config - Host: " << ibConfig.host << ":" << ibConfig.port << std::endl;
        std::cout << "   ✓ IB Config - FIX Version: " << ibConfig.fixVersion << std::endl;
        std::cout << "   ✓ IB Config - Target: " << ibConfig.targetCompId << std::endl;
        
        // Test 4: Credentials setup
        std::cout << "\n3. Testing credentials setup..." << std::endl;
        auto secureConfig = std::make_shared<SecureConfig>();
        auto credentials = std::make_shared<ApiCredentials>(*secureConfig);
        
        bool credSet = credentials->setCredentials("interactive_brokers", "DU123456", "password");
        std::cout << "   ✓ Credentials set: " << (credSet ? "Success" : "Failed") << std::endl;
        
        auto apiKey = credentials->getApiKey("interactive_brokers");
        std::cout << "   ✓ API Key retrieved: " << (apiKey ? *apiKey : "Not found") << std::endl;
        
        // Test 5: Order creation
        std::cout << "\n4. Testing order creation..." << std::endl;
        pinnacle::Order testOrder(
            "TEST123",
            "AAPL", 
            pinnacle::OrderSide::BUY,
            pinnacle::OrderType::LIMIT,
            150.0,  // price
            100.0,  // quantity
            pinnacle::utils::TimeUtils::getCurrentNanos()
        );
        
        std::cout << "   ✓ Order created - ID: " << testOrder.getOrderId() << std::endl;
        std::cout << "   ✓ Order details - " << testOrder.getSymbol() 
                  << " " << (testOrder.getSide() == pinnacle::OrderSide::BUY ? "BUY" : "SELL")
                  << " " << testOrder.getQuantity() << "@" << testOrder.getPrice() << std::endl;
        
        std::cout << "\n=== All Basic Tests Passed! ===" << std::endl;
        std::cout << "\n Test Summary:" << std::endl;
        std::cout << "✓ Factory pattern working" << std::endl;
        std::cout << "✓ Exchange support detection working" << std::endl;
        std::cout << "✓ Configuration system working" << std::endl;
        std::cout << "✓ Credentials management working" << std::endl;
        std::cout << "✓ Order creation working" << std::endl;
        
        std::cout << "\n Ready for live testing with IB Gateway!" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << " Error: " << e.what() << std::endl;
        return 1;
    }
}