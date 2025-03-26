#include "LockFreeOrderBook.h"
#include "../utils/TimeUtils.h"

namespace pinnacle {

LockFreeOrderBook::LockFreeOrderBook(const std::string& symbol)
    : OrderBook(symbol),
      m_lockFreeOrderBook(std::make_unique<utils::LockFreeOrderBook>(symbol)) {
    
    // Register for updates from the lock-free order book
    m_lockFreeOrderBook->registerUpdateCallback(
        [this](const utils::LockFreeOrderBook&) {
            this->onLockFreeOrderBookUpdate();
        }
    );
}

LockFreeOrderBook::~LockFreeOrderBook() = default;

void LockFreeOrderBook::onLockFreeOrderBookUpdate() {
    // Manually trigger all callbacks with this order book
    for (const auto& callback : m_callbacks) {
        callback(*this);
    }
}

bool LockFreeOrderBook::addOrder(std::shared_ptr<Order> order) {
    return m_lockFreeOrderBook->addOrder(order);
}

bool LockFreeOrderBook::cancelOrder(const std::string& orderId) {
    return m_lockFreeOrderBook->cancelOrder(orderId);
}

bool LockFreeOrderBook::executeOrder(const std::string& orderId, double quantity) {
    return m_lockFreeOrderBook->executeOrder(orderId, quantity);
}

double LockFreeOrderBook::executeMarketOrder(OrderSide side, double quantity, 
                                          std::vector<std::pair<std::string, double>>& fills) {
    return m_lockFreeOrderBook->executeMarketOrder(side, quantity, fills);
}

std::shared_ptr<Order> LockFreeOrderBook::getOrder(const std::string& orderId) const {
    return m_lockFreeOrderBook->getOrder(orderId);
}

double LockFreeOrderBook::getBestBidPrice() const {
    return m_lockFreeOrderBook->getBestBidPrice();
}

double LockFreeOrderBook::getBestAskPrice() const {
    return m_lockFreeOrderBook->getBestAskPrice();
}

double LockFreeOrderBook::getMidPrice() const {
    return m_lockFreeOrderBook->getMidPrice();
}

double LockFreeOrderBook::getSpread() const {
    return m_lockFreeOrderBook->getSpread();
}

size_t LockFreeOrderBook::getOrderCount() const {
    return m_lockFreeOrderBook->getOrderCount();
}

size_t LockFreeOrderBook::getBidLevels() const {
    return m_lockFreeOrderBook->getBidLevels();
}

size_t LockFreeOrderBook::getAskLevels() const {
    return m_lockFreeOrderBook->getAskLevels();
}

double LockFreeOrderBook::getVolumeAtPrice(double price) const {
    return m_lockFreeOrderBook->getVolumeAtPrice(price);
}

std::vector<PriceLevel> LockFreeOrderBook::getBidLevels(size_t depth) const {
    std::vector<PriceLevel> result;
    auto lockFreeLevels = m_lockFreeOrderBook->getBidLevels(depth);
    
    // Convert lock-free price levels to regular price levels
    for (const auto& lockFreeLevel : lockFreeLevels) {
        PriceLevel level(lockFreeLevel->getPrice());
        
        // Add orders to the level
        auto orders = lockFreeLevel->getOrders();
        for (const auto& order : orders) {
            level.addOrder(order);
        }
        
        result.push_back(level);
    }
    
    return result;
}

std::vector<PriceLevel> LockFreeOrderBook::getAskLevels(size_t depth) const {
    std::vector<PriceLevel> result;
    auto lockFreeLevels = m_lockFreeOrderBook->getAskLevels(depth);
    
    // Convert lock-free price levels to regular price levels
    for (const auto& lockFreeLevel : lockFreeLevels) {
        PriceLevel level(lockFreeLevel->getPrice());
        
        // Add orders to the level
        auto orders = lockFreeLevel->getOrders();
        for (const auto& order : orders) {
            level.addOrder(order);
        }
        
        result.push_back(level);
    }
    
    return result;
}

double LockFreeOrderBook::calculateMarketImpact(OrderSide side, double quantity) const {
    return m_lockFreeOrderBook->calculateMarketImpact(side, quantity);
}

double LockFreeOrderBook::calculateVolumeWeightedAveragePrice(OrderSide side, double quantity) const {
    return m_lockFreeOrderBook->calculateVolumeWeightedAveragePrice(side, quantity);
}

double LockFreeOrderBook::calculateOrderBookImbalance(size_t depth) const {
    return m_lockFreeOrderBook->calculateOrderBookImbalance(depth);
}

std::shared_ptr<OrderBookSnapshot> LockFreeOrderBook::getSnapshot() const {
    // Create a snapshot using the available methods
    auto snapshot = std::make_shared<OrderBookSnapshot>(
        getSymbol(),
        utils::TimeUtils::getCurrentNanos(),
        getBidLevels(std::numeric_limits<size_t>::max()),
        getAskLevels(std::numeric_limits<size_t>::max())
    );
    
    return snapshot;
}

void LockFreeOrderBook::clear() {
    m_lockFreeOrderBook->clear();
}

void LockFreeOrderBook::registerUpdateCallback(OrderBookUpdateCallback callback) {
    // Store the callback in our local list
    m_callbacks.push_back(callback);

    // No need to call the base class method since not directly handling callbacks
}

} // namespace pinnacle