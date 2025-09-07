/* ------------------------------------------------------------------
 * Copyright (C) 2024 StreamDAB Project
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2022 Matthias P. Braendli
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * ------------------------------------------------------------------- */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <queue>
#include "enhanced_stream.h"
#include "thai_metadata.h"

namespace StreamDAB {

/*! \file api_interface.h
 *  \brief StreamDAB integration API with RESTful HTTP endpoints and
 *         WebSocket support with MessagePack protocol for real-time communication.
 */

// HTTP status codes
enum class HttpStatus {
    OK = 200,
    Created = 201,
    BadRequest = 400,
    Unauthorized = 401,
    NotFound = 404,
    MethodNotAllowed = 405,
    InternalServerError = 500
};

// API request/response structures
struct ApiRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::string body;
    std::chrono::steady_clock::time_point timestamp;
};

struct ApiResponse {
    HttpStatus status;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string content_type = "application/json";
};

// WebSocket message types
enum class WebSocketMessageType {
    Status,
    Metadata,
    QualityMetrics,
    Error,
    ConfigUpdate,
    StreamEvent
};

struct WebSocketMessage {
    WebSocketMessageType type;
    std::string data; // MessagePack serialized data
    std::chrono::steady_clock::time_point timestamp;
    std::string client_id;
};

// Configuration structures
struct ApiConfig {
    int port = 8007;                    // StreamDAB allocation plan
    std::string bind_address = "0.0.0.0";
    bool enable_ssl = true;
    std::string ssl_cert_path;
    std::string ssl_key_path;
    std::string api_key;
    bool require_auth = true;
    int max_connections = 100;
    int request_timeout_ms = 30000;
    bool enable_cors = true;
    std::vector<std::string> allowed_origins;
    bool enable_rate_limiting = true;
    int rate_limit_requests_per_minute = 1000;
};

// Forward declarations
class HttpServer;
class WebSocketServer;
class MessagePackSerializer;

// Main API interface class
class StreamDABApiInterface {
private:
    ApiConfig config_;
    std::unique_ptr<HttpServer> http_server_;
    std::unique_ptr<WebSocketServer> websocket_server_;
    std::unique_ptr<MessagePackSerializer> serializer_;
    
    // Audio processing components
    std::shared_ptr<EnhancedStreamProcessor> stream_processor_;
    std::shared_ptr<ThaiMetadataProcessor> metadata_processor_;
    
    // Server state
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::thread websocket_thread_;
    std::thread status_broadcast_thread_;
    
    // Client management
    struct ConnectedClient {
        std::string client_id;
        std::chrono::steady_clock::time_point connected_time;
        std::string user_agent;
        std::string ip_address;
        bool subscribed_to_status = false;
        bool subscribed_to_metadata = false;
        bool subscribed_to_metrics = false;
    };
    
    std::map<std::string, ConnectedClient> connected_clients_;
    std::mutex clients_mutex_;
    std::condition_variable status_update_cv_;
    
    // Rate limiting
    struct RateLimitEntry {
        std::chrono::steady_clock::time_point window_start;
        int request_count;
    };
    std::map<std::string, RateLimitEntry> rate_limit_map_;
    std::mutex rate_limit_mutex_;
    
    // Metrics and monitoring
    struct ApiMetrics {
        size_t total_requests = 0;
        size_t successful_requests = 0;
        size_t failed_requests = 0;
        size_t websocket_connections = 0;
        std::chrono::steady_clock::time_point start_time;
        double average_response_time_ms = 0.0;
        size_t active_clients = 0;
    };
    ApiMetrics metrics_;
    std::mutex metrics_mutex_;

    // Request handlers
    ApiResponse handle_get_status(const ApiRequest& request);
    ApiResponse handle_get_metadata(const ApiRequest& request);
    ApiResponse handle_get_quality_metrics(const ApiRequest& request);
    ApiResponse handle_get_stream_info(const ApiRequest& request);
    ApiResponse handle_post_stream_config(const ApiRequest& request);
    ApiResponse handle_post_reconnect(const ApiRequest& request);
    ApiResponse handle_get_health(const ApiRequest& request);
    ApiResponse handle_get_statistics(const ApiRequest& request);
    ApiResponse handle_get_thai_analysis(const ApiRequest& request);
    
    // WebSocket handlers
    void handle_websocket_connection(const std::string& client_id);
    void handle_websocket_message(const std::string& client_id, const std::string& message);
    void handle_websocket_disconnect(const std::string& client_id);
    
    // Utility methods
    bool authenticate_request(const ApiRequest& request);
    bool check_rate_limit(const std::string& client_ip);
    std::string generate_client_id();
    void broadcast_status_update();
    void broadcast_metadata_update(const ThaiMetadata& metadata);
    void broadcast_quality_metrics(const StreamQualityMetrics& metrics);
    
    // Background tasks
    void status_broadcast_loop();
    void cleanup_disconnected_clients();

public:
    StreamDABApiInterface(const ApiConfig& config);
    ~StreamDABApiInterface();
    
    // Server lifecycle
    bool initialize();
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Component integration
    void set_stream_processor(std::shared_ptr<EnhancedStreamProcessor> processor);
    void set_metadata_processor(std::shared_ptr<ThaiMetadataProcessor> processor);
    
    // Configuration management
    void update_config(const ApiConfig& new_config);
    const ApiConfig& get_config() const { return config_; }
    
    // Metrics and monitoring
    ApiMetrics get_api_metrics() const;
    void reset_metrics();
    
    // Health check
    struct HealthStatus {
        bool api_healthy = false;
        bool stream_healthy = false;
        bool websocket_healthy = false;
        std::vector<std::string> issues;
        std::chrono::steady_clock::time_point check_time;
    };
    
    HealthStatus get_health_status() const;
};

// HTTP server implementation
class HttpServer {
private:
    ApiConfig config_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    
    // Request routing
    std::map<std::string, std::function<ApiResponse(const ApiRequest&)>> route_handlers_;
    
    void setup_routes(StreamDABApiInterface* api);
    void server_loop();
    ApiResponse handle_request(const ApiRequest& request);
    ApiRequest parse_http_request(const std::string& raw_request);
    std::string format_http_response(const ApiResponse& response);

public:
    HttpServer(const ApiConfig& config, StreamDABApiInterface* api);
    ~HttpServer();
    
    bool start();
    void stop();
    bool is_running() const { return running_; }
};

// WebSocket server implementation
class WebSocketServer {
private:
    ApiConfig config_;
    StreamDABApiInterface* api_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    
    // Message queue for broadcasting
    std::queue<WebSocketMessage> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    void server_loop();
    void process_message_queue();
    std::string generate_websocket_key_response(const std::string& key);

public:
    WebSocketServer(const ApiConfig& config, StreamDABApiInterface* api);
    ~WebSocketServer();
    
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Message broadcasting
    void broadcast_message(const WebSocketMessage& message);
    void send_to_client(const std::string& client_id, const WebSocketMessage& message);
};

// MessagePack serialization
class MessagePackSerializer {
public:
    // Serialize different data types
    std::string serialize_status(const StreamQualityMetrics& metrics, 
                                const ThaiMetadata& metadata);
    std::string serialize_metadata(const ThaiMetadata& metadata);
    std::string serialize_quality_metrics(const StreamQualityMetrics& metrics);
    std::string serialize_stream_info(const std::string& url, 
                                     const std::string& format, 
                                     const std::string& bitrate);
    std::string serialize_error(const std::string& error_message, 
                               const std::string& error_code);
    
    // Deserialize incoming messages
    struct ConfigUpdate {
        std::string primary_url;
        std::vector<std::string> fallback_urls;
        bool enable_normalization;
        double target_level_db;
        bool is_valid;
    };
    
    ConfigUpdate deserialize_config_update(const std::string& data);
    
    // Generic serialization utilities
    template<typename T>
    std::string serialize_object(const T& object);
    
    template<typename T>
    T deserialize_object(const std::string& data);

private:
    // Internal MessagePack implementation details
    std::string pack_map(const std::map<std::string, std::string>& data);
    std::string pack_array(const std::vector<std::string>& data);
    std::string pack_string(const std::string& str);
    std::string pack_double(double value);
    std::string pack_bool(bool value);
    std::string pack_timestamp(const std::chrono::system_clock::time_point& time);
};

// SSL/TLS support
class SSLContext {
private:
    std::string cert_path_;
    std::string key_path_;
    bool initialized_ = false;

public:
    SSLContext(const std::string& cert_path, const std::string& key_path);
    ~SSLContext();
    
    bool initialize();
    bool is_initialized() const { return initialized_; }
    
    // SSL operations would be implemented with OpenSSL
    // For now, we'll provide the interface
};

// Utility functions
namespace ApiUtils {
    // HTTP utilities
    std::string url_encode(const std::string& input);
    std::string url_decode(const std::string& input);
    std::map<std::string, std::string> parse_query_string(const std::string& query);
    std::string format_timestamp(const std::chrono::system_clock::time_point& time);
    
    // JSON utilities (for non-MessagePack responses)
    std::string to_json(const StreamQualityMetrics& metrics);
    std::string to_json(const ThaiMetadata& metadata);
    std::string to_json(const StreamDABApiInterface::HealthStatus& health);
    std::string to_json(const StreamDABApiInterface::ApiMetrics& metrics);
    
    // Validation
    bool is_valid_stream_url(const std::string& url);
    bool is_valid_api_key(const std::string& api_key);
    bool is_valid_client_id(const std::string& client_id);
    
    // Security
    std::string generate_secure_token(size_t length = 32);
    std::string hash_api_key(const std::string& api_key);
    bool verify_api_key(const std::string& provided_key, const std::string& expected_hash);
    
    // CORS headers
    std::map<std::string, std::string> get_cors_headers(const std::string& origin = "*");
}

// Exception handling
enum class ApiErrorCode {
    None,
    InvalidRequest,
    AuthenticationFailed,
    RateLimitExceeded,
    StreamNotAvailable,
    ConfigurationError,
    InternalError,
    WebSocketError
};

class ApiException : public std::exception {
private:
    ApiErrorCode error_code_;
    std::string message_;
    HttpStatus http_status_;

public:
    ApiException(ApiErrorCode code, const std::string& message, 
                HttpStatus status = HttpStatus::InternalServerError)
        : error_code_(code), message_(message), http_status_(status) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    ApiErrorCode get_error_code() const { return error_code_; }
    HttpStatus get_http_status() const { return http_status_; }
};

} // namespace StreamDAB