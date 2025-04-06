#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <functional>
#include <memory>
#include <string>

namespace websocketpp_stub {
    // Forward declarations
    class connection_hdl {};
    
    namespace lib {
        template<typename T>
        using shared_ptr = std::shared_ptr<T>;
        
        using error_code = std::error_code;
        
        namespace asio {
            using io_service = boost::asio::io_context;
            
            // Timer types
            using steady_timer = boost::asio::steady_timer;
            
            // Stub timer functions
            template<typename Duration>
            std::chrono::milliseconds milliseconds(Duration d) {
                return std::chrono::milliseconds(d);
            }
        }
    }
    
    namespace frame {
        enum opcode {
            text,
            binary
        };
    }
    
    namespace close {
        enum status {
            normal = 1000
        };
    }
    
    namespace log {
        enum level {
            all_levels,
            error_level
        };
        
        enum elevel {
            all_errors
        };
        
        enum alevel {
            all_access
        };
    }
    
    namespace config {
        struct asio_client {
            struct message_type {
                typedef std::shared_ptr<message_type> ptr;
                std::string get_payload() const { return ""; }
            };
        };
        
        struct asio_tls_client {
            struct message_type {
                typedef std::shared_ptr<message_type> ptr;
                std::string get_payload() const { return ""; }
            };
        };
    }
    
    template<typename T>
    class client {
    public:
        // Stub message type
        struct message {
            typedef std::shared_ptr<message> ptr;
            
            std::string get_payload() const { return ""; }
        };
        
        typedef message::ptr message_ptr;
        
        // Interface methods with empty implementations
        void clear_access_channels(log::alevel) {}
        void clear_error_channels(log::elevel) {}
        void init_asio() {}
        void start_perpetual() {}
        
        // Set handlers
        template<typename Handler>
        void set_open_handler(Handler) {}
        
        template<typename Handler>
        void set_close_handler(Handler) {}
        
        template<typename Handler>
        void set_fail_handler(Handler) {}
        
        template<typename Handler>
        void set_message_handler(Handler) {}
        
        template<typename Handler>
        void set_tls_init_handler(Handler) {}
        
        // Empty run method
        void run() {}
        
        // Get connection
        struct connection {
            std::error_code get_ec() const { return {}; }
        };
        typedef std::shared_ptr<connection> connection_ptr;
        
        connection_ptr get_connection(const std::string&, std::error_code&) {
            return std::make_shared<connection>();
        }
        
        connection_ptr get_con_from_hdl(connection_hdl) {
            return std::make_shared<connection>();
        }
        
        // Connect
        void connect(connection_ptr) {}
        
        // Send
        template<typename Message>
        void send(connection_hdl, Message, frame::opcode, std::error_code&) {}
        
        // Close
        void close(connection_hdl, close::status, const std::string&, std::error_code&) {}
    };
}

namespace websocketpp = websocketpp_stub;