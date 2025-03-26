#include "LockFreeOrderBook.h"
#include "../utils/TimeUtils.h"
#include <algorithm>
#include <limits>

namespace pinnacle {
namespace utils {

// LockFreePriceLevel implementation

LockFreePriceLevel::LockFreePriceLevel(double price)
    : m_price(price), m_totalQuantity(0.0), m_head(nullptr), m_tail(nullptr), m_orderCount(0) {
    // Create a dummy node as sentinel
    auto dummy = new OrderNode(nullptr);
    m_head.store(dummy, std::memory_order_relaxed);
    m_tail.store(dummy, std::memory_order_relaxed);
}

LockFreePriceLevel::~LockFreePriceLevel() {
    // Free all nodes
    OrderNode* current = m_head.load(std::memory_order_relaxed);
    while (current) {
        OrderNode* next = current->next.load(std::memory_order_relaxed);
        delete current;
        current = next;
    }
}

void LockFreePriceLevel::updateTotalQuantity() {
    double total = 0.0;
    OrderNode* current = m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
    
    while (current) {
        if (current->order) {
            total += current->order->getRemainingQuantity();
        }
        current = current->next.load(std::memory_order_acquire);
    }
    
    m_totalQuantity.store(total, std::memory_order_release);
}

bool LockFreePriceLevel::addOrder(std::shared_ptr<Order> order) {
    if (!order) {
        return false;
    }
    
    // Create a new node
    OrderNode* newNode = new OrderNode(std::move(order));
    
    // Add to the end of the list using a Michael-Scott queue approach
    while (true) {
        OrderNode* tail = m_tail.load(std::memory_order_acquire);
        OrderNode* next = tail->next.load(std::memory_order_acquire);
        
        // Check if tail is still the last node
        if (tail == m_tail.load(std::memory_order_acquire)) {
            if (next == nullptr) {
                // Tail is pointing to the last node, try to append the new node
                if (tail->next.compare_exchange_weak(next, newNode, 
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
                    // Successfully appended the new node, try to update the tail
                    m_tail.compare_exchange_strong(tail, newNode, 
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed);
                    m_orderCount.fetch_add(1, std::memory_order_release);
                    updateTotalQuantity();
                    return true;
                }
            } else {
                // Tail is not pointing to the last node, try to help advance it
                m_tail.compare_exchange_strong(tail, next, 
                                             std::memory_order_release,
                                             std::memory_order_relaxed);
            }
        }
    }
}

bool LockFreePriceLevel::removeOrder(const std::string& orderId) {
    OrderNode* prev = m_head.load(std::memory_order_acquire);
    OrderNode* curr = prev->next.load(std::memory_order_acquire);
    
    bool found = false;
    while (curr != nullptr) {
        // Find the node to remove
        if (curr->order && curr->order->getOrderId() == orderId) {
            // Logical deletion: null out the order pointer
            found = true;
            curr->order = nullptr;
            m_orderCount.fetch_sub(1, std::memory_order_release);
            break;
        }
        
        prev = curr;
        curr = curr->next.load(std::memory_order_acquire);
    }
    
    // Update total quantity if an order was removed
    if (found) {
        updateTotalQuantity();
    }
    
    return found;
}

std::vector<std::shared_ptr<Order>> LockFreePriceLevel::getOrders() const {
    std::vector<std::shared_ptr<Order>> orders;
    OrderNode* current = m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
    
    while (current) {
        if (current->order) {
            orders.push_back(current->order);
        }
        current = current->next.load(std::memory_order_acquire);
    }
    
    return orders;
}

std::shared_ptr<Order> LockFreePriceLevel::findOrder(const std::string& orderId) const {
    OrderNode* current = m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
    
    while (current) {
        if (current->order && current->order->getOrderId() == orderId) {
            return current->order;
        }
        current = current->next.load(std::memory_order_acquire);
    }
    
    return nullptr;
}

void LockFreePriceLevel::forEachOrder(const std::function<void(std::shared_ptr<Order>)>& func) const {
    OrderNode* current = m_head.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
    
    while (current) {
        if (current->order) {
            func(current->order);
        }
        current = current->next.load(std::memory_order_acquire);
    }
}

// LockFreeOrderMap implementation

LockFreeOrderMap::ShardGuard::ShardGuard(std::atomic_flag& lock) : m_lock(lock) {
    while (m_lock.test_and_set(std::memory_order_acquire)) {
        // Spin until we acquire the lock
    }
}

LockFreeOrderMap::ShardGuard::~ShardGuard() {
    m_lock.clear(std::memory_order_release);
}

size_t LockFreeOrderMap::getShardIndex(const std::string& orderId) const {
    // Simple hash function
    size_t hash = std::hash<std::string>{}(orderId);
    return hash & SHARD_MASK;
}

bool LockFreeOrderMap::insert(const std::string& orderId, std::shared_ptr<Order> order) {
    size_t shardIdx = getShardIndex(orderId);
    Shard& shard = m_shards[shardIdx];
    
    ShardGuard guard(shard.lock);
    
    auto result = shard.orders.emplace(orderId, std::move(order));
    if (result.second) {
        m_orderCount.fetch_add(1, std::memory_order_release);
    }
    
    return result.second;
}

bool LockFreeOrderMap::erase(const std::string& orderId) {
    size_t shardIdx = getShardIndex(orderId);
    Shard& shard = m_shards[shardIdx];
    
    ShardGuard guard(shard.lock);
    
    size_t removed = shard.orders.erase(orderId);
    if (removed > 0) {
        m_orderCount.fetch_sub(1, std::memory_order_release);
        return true;
    }
    
    return false;
}

std::shared_ptr<Order> LockFreeOrderMap::find(const std::string& orderId) const {
    size_t shardIdx = getShardIndex(orderId);
    const Shard& shard = m_shards[shardIdx];
    
    ShardGuard guard(const_cast<std::atomic_flag&>(shard.lock));
    
    auto it = shard.orders.find(orderId);
    if (it != shard.orders.end()) {
        return it->second;
    }
    
    return nullptr;
}

bool LockFreeOrderMap::contains(const std::string& orderId) const {
    size_t shardIdx = getShardIndex(orderId);
    const Shard& shard = m_shards[shardIdx];
    
    ShardGuard guard(const_cast<std::atomic_flag&>(shard.lock));
    
    return shard.orders.find(orderId) != shard.orders.end();
}

void LockFreeOrderMap::clear() {
    for (auto& shard : m_shards) {
        ShardGuard guard(shard.lock);
        shard.orders.clear();
    }
    
    m_orderCount.store(0, std::memory_order_release);
}

// LockFreeOrderBook implementation

LockFreeOrderBook::LockFreeOrderBook(const std::string& symbol) : m_symbol(symbol) {
}

bool LockFreeOrderBook::addOrder(std::shared_ptr<Order> order) {
    if (!order || order->getSymbol() != m_symbol) {
        return false;
    }
    
    // Check if the order already exists
    if (m_orders.contains(order->getOrderId())) {
        return false;
    }
    
    // Add to the order map
    if (!m_orders.insert(order->getOrderId(), order)) {
        return false;
    }
    
    // Add to the appropriate price level
    double price = order->getPrice();
    
    // Get or create the price level
    std::shared_ptr<LockFreePriceLevel> level;
    if (order->isBuy()) {
        level = m_bids.findLevel(price);
        if (!level) {
            level = std::make_shared<LockFreePriceLevel>(price);
            m_bids.insertLevel(price, level);
        }
    } else {
        level = m_asks.findLevel(price);
        if (!level) {
            level = std::make_shared<LockFreePriceLevel>(price);
            m_asks.insertLevel(price, level);
        }
    }
    
    // Add the order to the level
    if (!level->addOrder(order)) {
        m_orders.erase(order->getOrderId());
        return false;
    }
    
    // Notify listeners
    notifyUpdate();
    
    return true;
}

bool LockFreeOrderBook::cancelOrder(const std::string& orderId) {
    // Find the order
    std::shared_ptr<Order> order = m_orders.find(orderId);
    if (!order) {
        return false;
    }
    
    // Cancel the order
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    if (!order->cancel(timestamp)) {
        return false;
    }
    
    // Remove from the price level
    double price = order->getPrice();
    bool removed = false;
    
    if (order->isBuy()) {
        std::shared_ptr<LockFreePriceLevel> level = m_bids.findLevel(price);
        if (level) {
            removed = level->removeOrder(orderId);
            
            // If the level is empty, remove it
            if (level->getOrderCount() == 0) {
                m_bids.removeLevel(price);
            }
        }
    } else {
        std::shared_ptr<LockFreePriceLevel> level = m_asks.findLevel(price);
        if (level) {
            removed = level->removeOrder(orderId);
            
            // If the level is empty, remove it
            if (level->getOrderCount() == 0) {
                m_asks.removeLevel(price);
            }
        }
    }
    
    // Remove from the order map
    if (removed) {
        m_orders.erase(orderId);
    }
    
    // Notify listeners
    notifyUpdate();
    
    return removed;
}

bool LockFreeOrderBook::executeOrder(const std::string& orderId, double quantity) {
    // Find the order
    std::shared_ptr<Order> order = m_orders.find(orderId);
    if (!order) {
        return false;
    }
    
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
        std::shared_ptr<LockFreePriceLevel> level = m_bids.findLevel(price);
        if (level) {
            // If order is fully filled, remove it
            if (order->getStatus() == OrderStatus::FILLED) {
                level->removeOrder(orderId);
                
                // If level is empty, remove it
                if (level->getOrderCount() == 0) {
                    m_bids.removeLevel(price);
                }
                
                // Remove from order map
                m_orders.erase(orderId);
            }
        }
    } else {
        std::shared_ptr<LockFreePriceLevel> level = m_asks.findLevel(price);
        if (level) {
            // If order is fully filled, remove it
            if (order->getStatus() == OrderStatus::FILLED) {
                level->removeOrder(orderId);
                
                // If level is empty, remove it
                if (level->getOrderCount() == 0) {
                    m_asks.removeLevel(price);
                }
                
                // Remove from order map
                m_orders.erase(orderId);
            }
        }
    }
    
    // Notify listeners
    notifyUpdate();
    
    return true;
}

double LockFreeOrderBook::executeMarketOrder(OrderSide side, double quantity, 
                                          std::vector<std::pair<std::string, double>>& fills) {
    // Clear the fills vector
    fills.clear();
    
    // Validate quantity
    if (quantity <= 0) {
        return 0.0;
    }
    
    double remainingQuantity = quantity;
    double executedQuantity = 0.0;
    uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
    
    if (side == OrderSide::BUY) {
        // Buy order executes against asks
        auto askLevels = m_asks.getLevels(std::numeric_limits<size_t>::max());
        
        for (auto& level : askLevels) {
            if (remainingQuantity <= 0) {
                break;
            }
            
            // Get all orders at this level
            auto orders = level->getOrders();
            
            // Execute against orders at this level
            for (auto& order : orders) {
                if (remainingQuantity <= 0) {
                    break;
                }
                
                if (order && order->isActive()) {
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
                        
                        // If order is fully filled, remove it
                        if (order->getStatus() == OrderStatus::FILLED) {
                            level->removeOrder(order->getOrderId());
                            m_orders.erase(order->getOrderId());
                        }
                    }
                }
            }
            
            // If level is empty, remove it
            if (level->getOrderCount() == 0) {
                m_asks.removeLevel(level->getPrice());
            }
        }
    } else {
        // Sell order executes against bids
        auto bidLevels = m_bids.getLevels(std::numeric_limits<size_t>::max());
        
        for (auto& level : bidLevels) {
            if (remainingQuantity <= 0) {
                break;
            }
            
            // Get all orders at this level
            auto orders = level->getOrders();
            
            // Execute against orders at this level
            for (auto& order : orders) {
                if (remainingQuantity <= 0) {
                    break;
                }
                
                if (order && order->isActive()) {
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
                        
                        // If order is fully filled, remove it
                        if (order->getStatus() == OrderStatus::FILLED) {
                            level->removeOrder(order->getOrderId());
                            m_orders.erase(order->getOrderId());
                        }
                    }
                }
            }
            
            // If level is empty, remove it
            if (level->getOrderCount() == 0) {
                m_bids.removeLevel(level->getPrice());
            }
        }
    }
    
    // Notify listeners if any fills occurred
    if (executedQuantity > 0) {
        notifyUpdate();
    }
    
    return executedQuantity;
}

std::shared_ptr<Order> LockFreeOrderBook::getOrder(const std::string& orderId) const {
    return m_orders.find(orderId);
}

double LockFreeOrderBook::getBestBidPrice() const {
    auto bestBid = m_bids.getBestLevel();
    if (bestBid) {
        return bestBid->first;
    }
    return 0.0;
}

double LockFreeOrderBook::getBestAskPrice() const {
    auto bestAsk = m_asks.getBestLevel();
    if (bestAsk) {
        return bestAsk->first;
    }
    return std::numeric_limits<double>::max();
}

double LockFreeOrderBook::getMidPrice() const {
    double bestBid = getBestBidPrice();
    double bestAsk = getBestAskPrice();
    
    if (bestBid > 0.0 && bestAsk < std::numeric_limits<double>::max()) {
        return (bestBid + bestAsk) / 2.0;
    } else if (bestBid > 0.0) {
        return bestBid;
    } else if (bestAsk < std::numeric_limits<double>::max()) {
        return bestAsk;
    }
    
    return 0.0;
}

double LockFreeOrderBook::getSpread() const {
    double bestBid = getBestBidPrice();
    double bestAsk = getBestAskPrice();
    
    if (bestBid > 0.0 && bestAsk < std::numeric_limits<double>::max()) {
        return bestAsk - bestBid;
    }
    
    return 0.0;
}

size_t LockFreeOrderBook::getOrderCount() const {
    return m_orders.size();
}

size_t LockFreeOrderBook::getBidLevels() const {
    return m_bids.size();
}

size_t LockFreeOrderBook::getAskLevels() const {
    return m_asks.size();
}

double LockFreeOrderBook::getVolumeAtPrice(double price) const {
    // Check bids
    std::shared_ptr<LockFreePriceLevel> bidLevel = m_bids.findLevel(price);
    if (bidLevel) {
        return bidLevel->getTotalQuantity();
    }
    
    // Check asks
    std::shared_ptr<LockFreePriceLevel> askLevel = m_asks.findLevel(price);
    if (askLevel) {
        return askLevel->getTotalQuantity();
    }
    
    return 0.0;
}

std::vector<std::shared_ptr<LockFreePriceLevel>> LockFreeOrderBook::getBidLevels(size_t depth) const {
    return m_bids.getLevels(depth);
}

std::vector<std::shared_ptr<LockFreePriceLevel>> LockFreeOrderBook::getAskLevels(size_t depth) const {
    return m_asks.getLevels(depth);
}

double LockFreeOrderBook::calculateMarketImpact(OrderSide side, double quantity) const {
    double remainingQuantity = quantity;
    double weightedPrice = 0.0;
    double totalExecutedQuantity = 0.0;
    
    if (side == OrderSide::BUY) {
        // Calculate impact of a buy order
        auto askLevels = m_asks.getLevels(std::numeric_limits<size_t>::max());
        
        for (const auto& level : askLevels) {
            if (remainingQuantity <= 0) {
                break;
            }
            
            double levelQuantity = level->getTotalQuantity();
            double executedQuantity = std::min(remainingQuantity, levelQuantity);
            
            weightedPrice += level->getPrice() * executedQuantity;
            totalExecutedQuantity += executedQuantity;
            remainingQuantity -= executedQuantity;
        }
    } else {
        // Calculate impact of a sell order
        auto bidLevels = m_bids.getLevels(std::numeric_limits<size_t>::max());
        
        for (const auto& level : bidLevels) {
            if (remainingQuantity <= 0) {
                break;
            }
            
            double levelQuantity = level->getTotalQuantity();
            double executedQuantity = std::min(remainingQuantity, levelQuantity);
            
            weightedPrice += level->getPrice() * executedQuantity;
            totalExecutedQuantity += executedQuantity;
            remainingQuantity -= executedQuantity;
        }
    }
    
    // Calculate average execution price
    if (totalExecutedQuantity > 0) {
        return weightedPrice / totalExecutedQuantity;
    }
    
    // Return best available price if no execution possible
    if (side == OrderSide::BUY) {
        double bestAsk = getBestAskPrice();
        if (bestAsk < std::numeric_limits<double>::max()) {
            return bestAsk;
        }
    } else {
        double bestBid = getBestBidPrice();
        if (bestBid > 0) {
            return bestBid;
        }
    }
    
    return 0.0;
}

double LockFreeOrderBook::calculateVolumeWeightedAveragePrice(OrderSide side, double quantity) const {
    return calculateMarketImpact(side, quantity);
}

double LockFreeOrderBook::calculateOrderBookImbalance(size_t depth) const {
    double bidVolume = 0.0;
    double askVolume = 0.0;
    
    // Get bid levels
    auto bidLevels = m_bids.getLevels(depth);
    for (const auto& level : bidLevels) {
        bidVolume += level->getTotalQuantity();
    }
    
    // Get ask levels
    auto askLevels = m_asks.getLevels(depth);
    for (const auto& level : askLevels) {
        askVolume += level->getTotalQuantity();
    }
    
    // Calculate imbalance ratio
    double totalVolume = bidVolume + askVolume;
    if (totalVolume > 0) {
        return (bidVolume - askVolume) / totalVolume;
    }
    
    return 0.0;
}

void LockFreeOrderBook::clear() {
    m_bids.clear();
    m_asks.clear();
    m_orders.clear();
    
    notifyUpdate();
}

void LockFreeOrderBook::registerUpdateCallback(OrderBookUpdateCallback callback) {
    // Acquire lock for callback registration
    while (m_callbackLock.test_and_set(std::memory_order_acquire)) {
        // Spin until we acquire the lock
    }
    
    m_updateCallbacks.push_back(std::move(callback));
    
    m_callbackLock.clear(std::memory_order_release);
}

void LockFreeOrderBook::notifyUpdate() {
    // Increment update count
    m_updateCount.fetch_add(1, std::memory_order_release);
    
    // Make a local copy of callbacks
    std::vector<OrderBookUpdateCallback> callbacks;
    
    // Acquire lock
    while (m_callbackLock.test_and_set(std::memory_order_acquire)) {
        // Spin until we acquire the lock
    }
    
    callbacks = m_updateCallbacks;
    
    // Release lock
    m_callbackLock.clear(std::memory_order_release);
    
    // Notify all callbacks
    for (const auto& callback : callbacks) {
        callback(*this);
    }
}

} // namespace utils
} // namespace pinnacle