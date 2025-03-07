#include "OrderBook.h"
#include "../utils/TimeUtils.h"

#include <algorithm>
#include <mutex>
#include <limits>
#include <stdexcept>

namespace pinnacle {

// PriceLevel implementation
PriceLevel::PriceLevel(double price) : price(price), totalQuantity(0.0) {}

// Add a default constructor to resolve the map operator[] issue
PriceLevel::PriceLevel() : price(0.0), totalQuantity(0.0) {}

void PriceLevel::addOrder(std::shared_ptr<Order> order) {
    orders.push_back(order);
    updateTotalQuantity();
}

bool PriceLevel::removeOrder(const std::string& orderId) {
    auto it = std::find_if(orders.begin(), orders.end(), 
                        [&orderId](const std::shared_ptr<Order>& order) {
                            return order->getOrderId() == orderId;
                        });
    
    if (it != orders.end()) {
        orders.erase(it);
        updateTotalQuantity();
        return true;
    }
    
    return false;
}

void PriceLevel::updateTotalQuantity() {
    totalQuantity = 0.0;
    for (const auto& order : orders) {
        totalQuantity += order->getRemainingQuantity();
    }
}

// OrderBook implementation
OrderBook::OrderBook(const std::string& symbol) : m_symbol(symbol) {}

bool OrderBook::addOrder(std::shared_ptr<Order> order) {
    if (!order || order->getSymbol() != m_symbol) {
        return false;
    }
    
    // Acquire write lock
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    // Check if order already exists
    if (m_orders.find(order->getOrderId()) != m_orders.end()) {
        return false;
    }
    
    // Add order to the map
    m_orders[order->getOrderId()] = order;
    
    // Add order to the appropriate price level
    double price = order->getPrice();
    if (order->isBuy()) {
        // Find or create bid price level - use structured binding with try_emplace
        auto [bidIt, inserted] = m_bids.try_emplace(price, price);
        bidIt->second.addOrder(order);
    } else {
        // Find or create ask price level - use structured binding with try_emplace
        auto [askIt, inserted] = m_asks.try_emplace(price, price);
        askIt->second.addOrder(order);
    }
    
    // Update order count
    m_orderCount.fetch_add(1, std::memory_order_relaxed);
    
    // Notify listeners
    lock.unlock();
    notifyUpdate();
    
    return true;
}

bool OrderBook::cancelOrder(const std::string& orderId) {
    // Acquire write lock
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    // Find the order
    auto orderIt = m_orders.find(orderId);
    if (orderIt == m_orders.end()) {
        return false;
    }
    
    std::shared_ptr<Order> order = orderIt->second;
    
    // Cancel the order
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    if (!order->cancel(timestamp)) {
        return false;
    }
    
    // Remove the order from the price level
    double price = order->getPrice();
    bool removed = false;
    
    if (order->isBuy()) {
        auto bidIt = m_bids.find(price);
        if (bidIt != m_bids.end()) {
            removed = bidIt->second.removeOrder(orderId);
            
            // Remove the price level if it's empty
            if (bidIt->second.orders.empty()) {
                m_bids.erase(bidIt);
            }
        }
    } else {
        auto askIt = m_asks.find(price);
        if (askIt != m_asks.end()) {
            removed = askIt->second.removeOrder(orderId);
            
            // Remove the price level if it's empty
            if (askIt->second.orders.empty()) {
                m_asks.erase(askIt);
            }
        }
    }
    
    // Remove the order from the map
    m_orders.erase(orderIt);
    
    // Update order count
    if (removed) {
        m_orderCount.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Notify listeners
    lock.unlock();
    notifyUpdate();
    
    return removed;
}

bool OrderBook::executeOrder(const std::string& orderId, double quantity) {
    // Acquire write lock
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    // Find the order
    auto orderIt = m_orders.find(orderId);
    if (orderIt == m_orders.end()) {
        return false;
    }
    
    std::shared_ptr<Order> order = orderIt->second;
    
    // Check if quantity is valid
    if (quantity <= 0 || quantity > order->getRemainingQuantity()) {
        return false;
    }
    
    // Execute the order
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    if (!order->fill(quantity, timestamp)) {
        return false;
    }
    
    // Update the price level
    double price = order->getPrice();
    
    if (order->isBuy()) {
        auto bidIt = m_bids.find(price);
        if (bidIt != m_bids.end()) {
            bidIt->second.updateTotalQuantity();
            
            // Remove the order if it's fully filled
            if (order->getStatus() == OrderStatus::FILLED) {
                bidIt->second.removeOrder(orderId);
                
                // Remove the price level if it's empty
                if (bidIt->second.orders.empty()) {
                    m_bids.erase(bidIt);
                }
                
                // Remove the order from the map
                m_orders.erase(orderIt);
                
                // Update order count
                m_orderCount.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    } else {
        auto askIt = m_asks.find(price);
        if (askIt != m_asks.end()) {
            askIt->second.updateTotalQuantity();
            
            // Remove the order if it's fully filled
            if (order->getStatus() == OrderStatus::FILLED) {
                askIt->second.removeOrder(orderId);
                
                // Remove the price level if it's empty
                if (askIt->second.orders.empty()) {
                    m_asks.erase(askIt);
                }
                
                // Remove the order from the map
                m_orders.erase(orderIt);
                
                // Update order count
                m_orderCount.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }
    
    // Notify listeners
    lock.unlock();
    notifyUpdate();
    
    return true;
}

double OrderBook::executeMarketOrder(OrderSide side, double quantity, 
                                   std::vector<std::pair<std::string, double>>& fills) {
    // Clear the fills vector
    fills.clear();
    
    // Validate quantity
    if (quantity <= 0) {
        return 0.0;
    }
    
    // Acquire write lock
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    double remainingQuantity = quantity;
    double executedQuantity = 0.0;
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    
    // Execute against the opposite side of the book
    if (side == OrderSide::BUY) {
        // Buy order executes against asks
        for (auto askIt = m_asks.begin(); askIt != m_asks.end() && remainingQuantity > 0;) {
            PriceLevel& level = askIt->second;
            
            // Execute against orders at this level
            for (auto orderIt = level.orders.begin(); orderIt != level.orders.end() && remainingQuantity > 0;) {
                std::shared_ptr<Order> order = *orderIt;
                
                // Calculate fill quantity
                double orderRemaining = order->getRemainingQuantity();
                double fillQuantity = std::min(remainingQuantity, orderRemaining);
                
                // Fill the order
                if (order->fill(fillQuantity, timestamp)) {
                    // Record the fill
                    fills.emplace_back(order->getOrderId(), fillQuantity);
                    
                    // Update remaining quantity
                    remainingQuantity -= fillQuantity;
                    executedQuantity += fillQuantity;
                    
                    // Remove fully filled orders
                    if (order->getStatus() == OrderStatus::FILLED) {
                        orderIt = level.orders.erase(orderIt);
                        m_orders.erase(order->getOrderId());
                        m_orderCount.fetch_sub(1, std::memory_order_relaxed);
                    } else {
                        ++orderIt;
                    }
                } else {
                    ++orderIt;
                }
            }
            
            // Update level quantity
            level.updateTotalQuantity();
            
            // Remove empty levels
            if (level.orders.empty()) {
                askIt = m_asks.erase(askIt);
            } else {
                ++askIt;
            }
        }
    } else {
        // Sell order executes against bids
        for (auto bidIt = m_bids.begin(); bidIt != m_bids.end() && remainingQuantity > 0;) {
            PriceLevel& level = bidIt->second;
            
            // Execute against orders at this level
            for (auto orderIt = level.orders.begin(); orderIt != level.orders.end() && remainingQuantity > 0;) {
                std::shared_ptr<Order> order = *orderIt;
                
                // Calculate fill quantity
                double orderRemaining = order->getRemainingQuantity();
                double fillQuantity = std::min(remainingQuantity, orderRemaining);
                
                // Fill the order
                if (order->fill(fillQuantity, timestamp)) {
                    // Record the fill
                    fills.emplace_back(order->getOrderId(), fillQuantity);
                    
                    // Update remaining quantity
                    remainingQuantity -= fillQuantity;
                    executedQuantity += fillQuantity;
                    
                    // Remove fully filled orders
                    if (order->getStatus() == OrderStatus::FILLED) {
                        orderIt = level.orders.erase(orderIt);
                        m_orders.erase(order->getOrderId());
                        m_orderCount.fetch_sub(1, std::memory_order_relaxed);
                    } else {
                        ++orderIt;
                    }
                } else {
                    ++orderIt;
                }
            }
            
            // Update level quantity
            level.updateTotalQuantity();
            
            // Remove empty levels
            if (level.orders.empty()) {
                bidIt = m_bids.erase(bidIt);
            } else {
                ++bidIt;
            }
        }
    }
    
    // Notify listeners if any fills occurred
    if (executedQuantity > 0) {
        lock.unlock();
        notifyUpdate();
    }
    
    return executedQuantity;
}

std::shared_ptr<Order> OrderBook::getOrder(const std::string& orderId) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    auto it = m_orders.find(orderId);
    if (it != m_orders.end()) {
        return it->second;
    }
    
    return nullptr;
}

double OrderBook::getBestBidPrice() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    if (!m_bids.empty()) {
        return m_bids.begin()->first;
    }
    
    return 0.0;
}

double OrderBook::getBestAskPrice() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    if (!m_asks.empty()) {
        return m_asks.begin()->first;
    }
    
    return std::numeric_limits<double>::max();
}

double OrderBook::getMidPrice() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    if (!m_bids.empty() && !m_asks.empty()) {
        double bestBid = m_bids.begin()->first;
        double bestAsk = m_asks.begin()->first;
        return (bestBid + bestAsk) / 2.0;
    } else if (!m_bids.empty()) {
        return m_bids.begin()->first;
    } else if (!m_asks.empty()) {
        return m_asks.begin()->first;
    }
    
    return 0.0;
}

double OrderBook::getSpread() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    if (!m_bids.empty() && !m_asks.empty()) {
        double bestBid = m_bids.begin()->first;
        double bestAsk = m_asks.begin()->first;
        return bestAsk - bestBid;
    }
    
    return 0.0;
}

size_t OrderBook::getOrderCount() const {
    return m_orderCount.load(std::memory_order_relaxed);
}

size_t OrderBook::getBidLevels() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_bids.size();
}

size_t OrderBook::getAskLevels() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_asks.size();
}

double OrderBook::getVolumeAtPrice(double price) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    // Check bids
    auto bidIt = m_bids.find(price);
    if (bidIt != m_bids.end()) {
        return bidIt->second.totalQuantity;
    }
    
    // Check asks
    auto askIt = m_asks.find(price);
    if (askIt != m_asks.end()) {
        return askIt->second.totalQuantity;
    }
    
    return 0.0;
}

std::vector<PriceLevel> OrderBook::getBidLevels(size_t depth) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    std::vector<PriceLevel> levels;
    size_t count = 0;
    
    for (const auto& pair : m_bids) {
        levels.push_back(pair.second);
        ++count;
        
        if (count >= depth) {
            break;
        }
    }
    
    return levels;
}

std::vector<PriceLevel> OrderBook::getAskLevels(size_t depth) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    std::vector<PriceLevel> levels;
    size_t count = 0;
    
    for (const auto& pair : m_asks) {
        levels.push_back(pair.second);
        ++count;
        
        if (count >= depth) {
            break;
        }
    }
    
    return levels;
}

double OrderBook::calculateMarketImpact(OrderSide side, double quantity) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    double remainingQuantity = quantity;
    double weightedPrice = 0.0;
    double totalExecutedQuantity = 0.0;
    
    if (side == OrderSide::BUY) {
        // Calculate impact of a buy order
        for (const auto& pair : m_asks) {
            const PriceLevel& level = pair.second;
            double levelQuantity = level.totalQuantity;
            
            if (remainingQuantity <= 0) {
                break;
            }
            
            double executedQuantity = std::min(remainingQuantity, levelQuantity);
            weightedPrice += level.price * executedQuantity;
            totalExecutedQuantity += executedQuantity;
            remainingQuantity -= executedQuantity;
        }
    } else {
        // Calculate impact of a sell order
        for (const auto& pair : m_bids) {
            const PriceLevel& level = pair.second;
            double levelQuantity = level.totalQuantity;
            
            if (remainingQuantity <= 0) {
                break;
            }
            
            double executedQuantity = std::min(remainingQuantity, levelQuantity);
            weightedPrice += level.price * executedQuantity;
            totalExecutedQuantity += executedQuantity;
            remainingQuantity -= executedQuantity;
        }
    }
    
    // Calculate average execution price
    if (totalExecutedQuantity > 0) {
        return weightedPrice / totalExecutedQuantity;
    }
    
    // Return best available price if no execution possible
    if (side == OrderSide::BUY && !m_asks.empty()) {
        return m_asks.begin()->first;
    } else if (side == OrderSide::SELL && !m_bids.empty()) {
        return m_bids.begin()->first;
    }
    
    return 0.0;
}

double OrderBook::calculateVolumeWeightedAveragePrice(OrderSide side, double quantity) const {
    return calculateMarketImpact(side, quantity);
}

double OrderBook::calculateOrderBookImbalance(size_t depth) const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    double bidVolume = 0.0;
    double askVolume = 0.0;
    
    // Calculate bid volume
    size_t bidCount = 0;
    for (const auto& pair : m_bids) {
        bidVolume += pair.second.totalQuantity;
        ++bidCount;
        
        if (bidCount >= depth) {
            break;
        }
    }
    
    // Calculate ask volume
    size_t askCount = 0;
    for (const auto& pair : m_asks) {
        askVolume += pair.second.totalQuantity;
        ++askCount;
        
        if (askCount >= depth) {
            break;
        }
    }
    
    // Calculate imbalance
    double totalVolume = bidVolume + askVolume;
    if (totalVolume > 0) {
        return (bidVolume - askVolume) / totalVolume;
    }
    
    return 0.0;
}

std::shared_ptr<OrderBookSnapshot> OrderBook::getSnapshot() const {
    // Acquire read lock
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    
    // Get current timestamp
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    
    // Create vectors for bids and asks
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    
    // Copy bid levels
    for (const auto& pair : m_bids) {
        bids.push_back(pair.second);
    }
    
    // Copy ask levels
    for (const auto& pair : m_asks) {
        asks.push_back(pair.second);
    }
    
    // Create and return the snapshot
    return std::make_shared<OrderBookSnapshot>(m_symbol, timestamp, std::move(bids), std::move(asks));
}

void OrderBook::clear() {
    // Acquire write lock
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    m_bids.clear();
    m_asks.clear();
    m_orders.clear();
    m_orderCount.store(0, std::memory_order_relaxed);
    
    // Notify listeners
    lock.unlock();
    notifyUpdate();
}

void OrderBook::registerUpdateCallback(OrderBookUpdateCallback callback) {
    // Acquire write lock for callback registration
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    
    m_updateCallbacks.push_back(std::move(callback));
}

void OrderBook::notifyUpdate() {
    // Increment update counter
    m_updateCount.fetch_add(1, std::memory_order_relaxed);
    
    // Create a local copy of callbacks while holding the read lock
    std::vector<OrderBookUpdateCallback> callbacks;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        callbacks = m_updateCallbacks;
    }
    
    // Notify all callbacks without holding the lock
    for (const auto& callback : callbacks) {
        callback(*this);
    }
}

bool OrderBook::matchOrder(std::shared_ptr<Order> order) {
    // This method should be called with a write lock already held
    
    if (!order || !order->isActive()) {
        return false;
    }
    
    double remainingQuantity = order->getRemainingQuantity();
    if (remainingQuantity <= 0) {
        return false;
    }
    
    bool matched = false;
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    
    if (order->isBuy()) {
        // Match buy order against asks
        for (auto askIt = m_asks.begin(); askIt != m_asks.end() && remainingQuantity > 0;) {
            // Check if price is acceptable
            if (askIt->first > order->getPrice()) {
                break;
            }
            
            PriceLevel& level = askIt->second;
            
            // Match against orders at this level
            for (auto orderIt = level.orders.begin(); orderIt != level.orders.end() && remainingQuantity > 0;) {
                std::shared_ptr<Order> makerOrder = *orderIt;
                
                // Check if orders can match
                if (canMatch(*order, *makerOrder)) {
                    // Calculate match quantity
                    double matchQuantity = std::min(remainingQuantity, makerOrder->getRemainingQuantity());
                    
                    // Fill both orders
                    if (order->fill(matchQuantity, timestamp) && makerOrder->fill(matchQuantity, timestamp)) {
                        matched = true;
                        remainingQuantity = order->getRemainingQuantity();
                        
                        // Remove fully filled maker orders
                        if (makerOrder->getStatus() == OrderStatus::FILLED) {
                            orderIt = level.orders.erase(orderIt);
                            m_orders.erase(makerOrder->getOrderId());
                            m_orderCount.fetch_sub(1, std::memory_order_relaxed);
                        } else {
                            ++orderIt;
                        }
                    } else {
                        ++orderIt;
                    }
                } else {
                    ++orderIt;
                }
            }
            
            // Update level quantity
            level.updateTotalQuantity();
            
            // Remove empty levels
            if (level.orders.empty()) {
                askIt = m_asks.erase(askIt);
            } else {
                ++askIt;
            }
        }
    } else {
        // Match sell order against bids
        for (auto bidIt = m_bids.begin(); bidIt != m_bids.end() && remainingQuantity > 0;) {
            // Check if price is acceptable
            if (bidIt->first < order->getPrice()) {
                break;
            }
            
            PriceLevel& level = bidIt->second;
            
            // Match against orders at this level
            for (auto orderIt = level.orders.begin(); orderIt != level.orders.end() && remainingQuantity > 0;) {
                std::shared_ptr<Order> makerOrder = *orderIt;
                
                // Check if orders can match
                if (canMatch(*order, *makerOrder)) {
                    // Calculate match quantity
                    double matchQuantity = std::min(remainingQuantity, makerOrder->getRemainingQuantity());
                    
                    // Fill both orders
                    if (order->fill(matchQuantity, timestamp) && makerOrder->fill(matchQuantity, timestamp)) {
                        matched = true;
                        remainingQuantity = order->getRemainingQuantity();
                        
                        // Remove fully filled maker orders
                        if (makerOrder->getStatus() == OrderStatus::FILLED) {
                            orderIt = level.orders.erase(orderIt);
                            m_orders.erase(makerOrder->getOrderId());
                            m_orderCount.fetch_sub(1, std::memory_order_relaxed);
                        } else {
                            ++orderIt;
                        }
                    } else {
                        ++orderIt;
                    }
                } else {
                    ++orderIt;
                }
            }
            
            // Update level quantity
            level.updateTotalQuantity();
            
            // Remove empty levels
            if (level.orders.empty()) {
                bidIt = m_bids.erase(bidIt);
            } else {
                ++bidIt;
            }
        }
    }
    
    return matched;
}

bool OrderBook::canMatch(const Order& takerOrder, const Order& makerOrder) const {
    // Orders must be on opposite sides
    if (takerOrder.getSide() == makerOrder.getSide()) {
        return false;
    }
    
    // Both orders must be active
    if (!takerOrder.isActive() || !makerOrder.isActive()) {
        return false;
    }
    
    // Price must be acceptable
    if (takerOrder.isBuy()) {
        return takerOrder.getPrice() >= makerOrder.getPrice();
    } else {
        return takerOrder.getPrice() <= makerOrder.getPrice();
    }
}

// OrderBookSnapshot implementation
OrderBookSnapshot::OrderBookSnapshot(
    const std::string& symbol,
    uint64_t timestamp,
    std::vector<PriceLevel> bids,
    std::vector<PriceLevel> asks
) : m_symbol(symbol),
    m_timestamp(timestamp),
    m_bids(std::move(bids)),
    m_asks(std::move(asks)) {
}

} // namespace pinnacle