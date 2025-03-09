#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace pinnacle {
namespace utils {

/**
 * @class TimeUtils
 * @brief Provides high-precision timing utilities for the trading system
 */
class TimeUtils {
public:
    /**
     * @brief Get current timestamp in nanoseconds
     * @return Current timestamp in nanoseconds since epoch
     */
    static uint64_t getCurrentNanos() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }

    /**
     * @brief Get current timestamp in microseconds
     * @return Current timestamp in microseconds since epoch
     */
    static uint64_t getCurrentMicros() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }

    /**
     * @brief Get current timestamp in milliseconds
     * @return Current timestamp in milliseconds since epoch
     */
    static uint64_t getCurrentMillis() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }

    /**
     * @brief Get current timestamp in seconds
     * @return Current timestamp in seconds since epoch
     */
    static uint64_t getCurrentSeconds() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
    }

    /**
     * @brief Convert nanoseconds to readable timestamp format
     * @param nanos Nanoseconds since epoch
     * @return Formatted timestamp string (YYYY-MM-DD HH:MM:SS.nanoseconds)
     */
    static std::string nanosToTimestamp(uint64_t nanos);

    /**
     * @brief Sleep for specified number of nanoseconds
     * @param nanos Number of nanoseconds to sleep
     * 
     * Note: Sleep accuracy is platform-dependent and may not be precise 
     * for very small values.
     */
    static void sleepForNanos(uint64_t nanos) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
    }

    /**
     * @brief Sleep for specified number of microseconds
     * @param micros Number of microseconds to sleep
     */
    static void sleepForMicros(uint64_t micros) {
        std::this_thread::sleep_for(std::chrono::microseconds(micros));
    }

    /**
     * @brief Sleep for specified number of milliseconds
     * @param millis Number of milliseconds to sleep
     */
    static void sleepForMillis(uint64_t millis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(millis));
    }

    /**
     * @brief Measure execution time of a function in nanoseconds
     * @param func Function to measure
     * @return Execution time in nanoseconds
     */
    template<typename Func>
    static uint64_t measureExecutionTimeNanos(Func&& func) {
        // auto start = std::chrono::high_resolution_clock::now();
        // std::forward<Func>(func)();
        // auto end = std::chrono::high_resolution_clock::now();
        
        // return std::chrono::duration_cast<std::chrono::nanoseconds>(
        //     end - start).count();
        auto start = std::chrono::steady_clock::now();
        std::forward<Func>(func)();
        auto end = std::chrono::steady_clock::now();

        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count();
    }

    /**
     * @brief Measure execution time of a function in microseconds
     * @param func Function to measure
     * @return Execution time in microseconds
     */
    template<typename Func>
    static uint64_t measureExecutionTimeMicros(Func&& func) {
        // auto start = std::chrono::high_resolution_clock::now();
        // std::forward<Func>(func)();
        // auto end = std::chrono::high_resolution_clock::now();
        
        // return std::chrono::duration_cast<std::chrono::microseconds>(
        //     end - start).count();
        auto start = std::chrono::steady_clock::now();
std::forward<Func>(func)();
auto end = std::chrono::steady_clock::now();

return std::chrono::duration_cast<std::chrono::microseconds>(
    end - start).count();
    }

    /**
     * @brief Calculate time difference in nanoseconds
     * @param start_nanos Start time in nanoseconds
     * @param end_nanos End time in nanoseconds
     * @return Time difference in nanoseconds
     */
    static uint64_t getDiffNanos(uint64_t start_nanos, uint64_t end_nanos) {
        return (end_nanos > start_nanos) ? (end_nanos - start_nanos) : 0;
    }

    /**
     * @brief Get current timestamp as ISO 8601 string
     * @return Current timestamp in ISO 8601 format
     */
    static std::string getCurrentISOTimestamp();

    /**
     * @brief Check if nanosecond-precision is available on this system
     * @return true if nanosecond precision is supported, false otherwise
     */
    static bool isNanosecondPrecisionAvailable();
};

} // namespace utils
} // namespace pinnacle