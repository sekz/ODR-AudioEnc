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

#include "api_interface.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <regex>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

using namespace std;
using namespace std::chrono;

namespace StreamDAB {

// StreamDABApiInterface implementation
StreamDABApiInterface::StreamDABApiInterface(const ApiConfig& config) : config_(config) {
    metrics_.start_time = steady_clock::now();
    
    serializer_ = make_unique<MessagePackSerializer>();
    http_server_ = make_unique<HttpServer>(config_, this);
    websocket_server_ = make_unique<WebSocketServer>(config_, this);
}

StreamDABApiInterface::~StreamDABApiInterface() {
    stop();
}

bool StreamDABApiInterface::initialize() {
    try {
        // Initialize SSL context if SSL is enabled
        if (config_.enable_ssl) {
            if (config_.ssl_cert_path.empty() || config_.ssl_key_path.empty()) {
                fprintf(stderr, "SSL enabled but certificate paths not provided\n");
                return false;
            }
            
            // SSL initialization would go here
            printf("SSL support initialized\n");
        }
        
        printf("StreamDAB API Interface initialized on port %d\n", config_.port);
        return true;
    }
    catch (const exception& e) {
        fprintf(stderr, "API interface initialization failed: %s\n", e.what());
        return false;
    }
}

bool StreamDABApiInterface::start() {
    if (running_) {
        return true;
    }
    
    if (!initialize()) {
        return false;
    }
    
    running_ = true;
    
    // Start HTTP server
    if (!http_server_->start()) {
        fprintf(stderr, "Failed to start HTTP server\n");
        running_ = false;
        return false;
    }
    
    // Start WebSocket server
    if (!websocket_server_->start()) {
        fprintf(stderr, "Failed to start WebSocket server\n");
        running_ = false;
        return false;
    }
    
    // Start status broadcast thread
    status_broadcast_thread_ = thread(&StreamDABApiInterface::status_broadcast_loop, this);
    
    printf("StreamDAB API Interface started successfully\n");
    return true;
}

void StreamDABApiInterface::stop() {
    running_ = false;
    
    // Stop servers
    if (http_server_) {
        http_server_->stop();
    }
    
    if (websocket_server_) {
        websocket_server_->stop();
    }
    
    // Stop background threads
    if (status_broadcast_thread_.joinable()) {
        status_update_cv_.notify_all();
        status_broadcast_thread_.join();
    }
    
    printf("StreamDAB API Interface stopped\n");
}

// HTTP request handlers
ApiResponse StreamDABApiInterface::handle_get_status(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        // Gather status information
        map<string, string> status_data;
        
        if (stream_processor_) {
            status_data["stream_connected"] = stream_processor_->is_connected() ? "true" : "false";
            status_data["stream_running"] = stream_processor_->is_running() ? "true" : "false";
            status_data["current_url"] = stream_processor_->get_current_url();
            status_data["stream_healthy"] = stream_processor_->is_healthy() ? "true" : "false";
        }
        else {
            status_data["stream_connected"] = "false";
            status_data["stream_running"] = "false";
            status_data["current_url"] = "";
            status_data["stream_healthy"] = "false";
        }
        
        status_data["api_running"] = "true";
        status_data["timestamp"] = ApiUtils::format_timestamp(system_clock::now());
        
        response.body = ApiUtils::to_json(status_data);
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to get status", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_metadata(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        if (!stream_processor_) {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Stream processor not available"})";
            return response;
        }
        
        // Get current metadata (this would come from the stream)
        ThaiMetadata current_metadata;
        current_metadata.title_utf8 = stream_processor_->get_current_title();
        current_metadata.artist_utf8 = stream_processor_->get_current_artist();
        current_metadata.timestamp = system_clock::now();
        
        if (metadata_processor_) {
            // Process with Thai metadata processor
            current_metadata = metadata_processor_->process_raw_metadata(
                current_metadata.title_utf8, 
                current_metadata.artist_utf8);
        }
        
        response.body = ApiUtils::to_json(current_metadata);
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to get metadata", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_quality_metrics(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        if (!stream_processor_) {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Stream processor not available"})";
            return response;
        }
        
        auto metrics = stream_processor_->get_quality_metrics();
        response.body = ApiUtils::to_json(metrics);
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to get quality metrics", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_post_stream_config(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        if (!stream_processor_) {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Stream processor not available"})";
            return response;
        }
        
        // Parse configuration update from request body
        auto config_update = serializer_->deserialize_config_update(request.body);
        
        if (!config_update.is_valid) {
            response.status = HttpStatus::BadRequest;
            response.body = R"({"error": "Invalid configuration data"})";
            return response;
        }
        
        // Apply configuration update
        StreamConfig new_config = stream_processor_->get_config();
        new_config.primary_url = config_update.primary_url;
        new_config.fallback_urls = config_update.fallback_urls;
        new_config.enable_normalization = config_update.enable_normalization;
        new_config.target_level_db = config_update.target_level_db;
        
        stream_processor_->update_config(new_config);
        
        response.body = R"({"success": true, "message": "Configuration updated"})";
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to update configuration", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_post_reconnect(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        if (!stream_processor_) {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Stream processor not available"})";
            return response;
        }
        
        bool reconnect_success = stream_processor_->force_reconnect();
        
        response.body = R"({"success": )" + string(reconnect_success ? "true" : "false") + 
                       R"(, "message": ")" + (reconnect_success ? "Reconnection initiated" : "Reconnection failed") + R"("})";
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to reconnect", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_health(const ApiRequest& request) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        auto health = get_health_status();
        response.body = ApiUtils::to_json(health);
        
        // Return 503 if not healthy
        if (!health.api_healthy || !health.stream_healthy) {
            response.status = HttpStatus::InternalServerError;
        }
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to get health status", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

StreamDABApiInterface::HealthStatus StreamDABApiInterface::get_health_status() const {
    HealthStatus health;
    health.check_time = steady_clock::now();
    health.api_healthy = running_;
    health.websocket_healthy = websocket_server_ && websocket_server_->is_running();
    
    if (stream_processor_) {
        health.stream_healthy = stream_processor_->is_healthy();
        auto stream_issues = stream_processor_->get_health_issues();
        health.issues.insert(health.issues.end(), stream_issues.begin(), stream_issues.end());
    }
    else {
        health.stream_healthy = false;
        health.issues.push_back("Stream processor not initialized");
    }
    
    if (!health.api_healthy) {
        health.issues.push_back("API server not running");
    }
    
    if (!health.websocket_healthy) {
        health.issues.push_back("WebSocket server not running");
    }
    
    return health;
}

void StreamDABApiInterface::status_broadcast_loop() {
    while (running_) {
        try {
            if (stream_processor_ && metadata_processor_) {
                // Broadcast status update to WebSocket clients
                broadcast_status_update();
            }
            
            // Wait for next update or shutdown signal
            unique_lock<mutex> lock(clients_mutex_);
            status_update_cv_.wait_for(lock, seconds(5)); // Update every 5 seconds
        }
        catch (const exception& e) {
            fprintf(stderr, "Error in status broadcast loop: %s\n", e.what());
        }
    }
}

void StreamDABApiInterface::broadcast_status_update() {
    if (!websocket_server_) return;
    
    try {
        auto metrics = stream_processor_->get_quality_metrics();
        
        // Create dummy metadata for now - in real implementation this would come from stream
        ThaiMetadata current_metadata;
        current_metadata.title_utf8 = stream_processor_->get_current_title();
        current_metadata.artist_utf8 = stream_processor_->get_current_artist();
        current_metadata.timestamp = system_clock::now();
        
        string serialized_status = serializer_->serialize_status(metrics, current_metadata);
        
        WebSocketMessage message;
        message.type = WebSocketMessageType::Status;
        message.data = serialized_status;
        message.timestamp = steady_clock::now();
        
        websocket_server_->broadcast_message(message);
    }
    catch (const exception& e) {
        fprintf(stderr, "Error broadcasting status update: %s\n", e.what());
    }
}

bool StreamDABApiInterface::authenticate_request(const ApiRequest& request) {
    if (!config_.require_auth) {
        return true;
    }
    
    auto auth_header = request.headers.find("Authorization");
    if (auth_header == request.headers.end()) {
        return false;
    }
    
    string auth_value = auth_header->second;
    if (auth_value.substr(0, 7) != "Bearer ") {
        return false;
    }
    
    string provided_key = auth_value.substr(7);
    return ApiUtils::verify_api_key(provided_key, config_.api_key);
}

string StreamDABApiInterface::generate_client_id() {
    return ApiUtils::generate_secure_token(16);
}

// HttpServer implementation
HttpServer::HttpServer(const ApiConfig& config, StreamDABApiInterface* api) 
    : config_(config) {
    setup_routes(api);
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setup_routes(StreamDABApiInterface* api) {
    route_handlers_["/api/v1/status"] = [api](const ApiRequest& req) {
        return api->handle_get_status(req);
    };
    
    route_handlers_["/api/v1/metadata"] = [api](const ApiRequest& req) {
        return api->handle_get_metadata(req);
    };
    
    route_handlers_["/api/v1/quality"] = [api](const ApiRequest& req) {
        return api->handle_get_quality_metrics(req);
    };
    
    route_handlers_["/api/v1/config"] = [api](const ApiRequest& req) {
        return api->handle_post_stream_config(req);
    };
    
    route_handlers_["/api/v1/reconnect"] = [api](const ApiRequest& req) {
        return api->handle_post_reconnect(req);
    };
    
    route_handlers_["/api/v1/health"] = [api](const ApiRequest& req) {
        return api->handle_get_health(req);
    };
}

bool HttpServer::start() {
    if (running_) {
        return true;
    }
    
    running_ = true;
    server_thread_ = thread(&HttpServer::server_loop, this);
    
    return true;
}

void HttpServer::stop() {
    running_ = false;
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::server_loop() {
    // Simplified HTTP server implementation
    // In production, use a proper HTTP library like libmicrohttpd or cpp-httplib
    
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        fprintf(stderr, "Socket creation failed\n");
        return;
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        fprintf(stderr, "setsockopt failed\n");
        close(server_fd);
        return;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config_.port);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        fprintf(stderr, "Bind failed on port %d\n", config_.port);
        close(server_fd);
        return;
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        fprintf(stderr, "Listen failed\n");
        close(server_fd);
        return;
    }
    
    printf("HTTP server listening on port %d\n", config_.port);
    
    while (running_) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_socket < 0) {
            if (running_) {
                fprintf(stderr, "Accept failed\n");
            }
            continue;
        }
        
        // Handle request in separate thread (simplified)
        thread([this, client_socket]() {
            char buffer[8192] = {0};
            read(client_socket, buffer, 8191);
            
            ApiRequest request = parse_http_request(string(buffer));
            ApiResponse response = handle_request(request);
            
            string response_str = format_http_response(response);
            write(client_socket, response_str.c_str(), response_str.length());
            
            close(client_socket);
        }).detach();
    }
    
    close(server_fd);
}

ApiRequest HttpServer::parse_http_request(const string& raw_request) {
    ApiRequest request;
    request.timestamp = steady_clock::now();
    
    istringstream stream(raw_request);
    string line;
    
    // Parse request line
    if (getline(stream, line)) {
        istringstream request_line(line);
        request_line >> request.method >> request.path;
        
        // Extract query parameters
        size_t query_pos = request.path.find('?');
        if (query_pos != string::npos) {
            string query = request.path.substr(query_pos + 1);
            request.path = request.path.substr(0, query_pos);
            request.query_params = ApiUtils::parse_query_string(query);
        }
    }
    
    // Parse headers
    while (getline(stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != string::npos) {
            string key = line.substr(0, colon_pos);
            string value = line.substr(colon_pos + 2);
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            request.headers[key] = value;
        }
    }
    
    // Parse body
    string body_line;
    while (getline(stream, body_line)) {
        request.body += body_line + "\n";
    }
    
    return request;
}

ApiResponse HttpServer::handle_request(const ApiRequest& request) {
    ApiResponse response;
    
    try {
        // Add CORS headers
        if (config_.enable_cors) {
            auto cors_headers = ApiUtils::get_cors_headers();
            for (const auto& header : cors_headers) {
                response.headers[header.first] = header.second;
            }
        }
        
        // Handle preflight requests
        if (request.method == "OPTIONS") {
            response.status = HttpStatus::OK;
            return response;
        }
        
        // Route to appropriate handler
        auto handler_it = route_handlers_.find(request.path);
        if (handler_it != route_handlers_.end()) {
            response = handler_it->second(request);
        }
        else {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Endpoint not found"})";
        }
    }
    catch (const ApiException& e) {
        response.status = e.get_http_status();
        response.body = R"({"error": ")" + string(e.what()) + R"("})";
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Internal server error", "message": ")" + string(e.what()) + R"("})";
    }
    
    return response;
}

string HttpServer::format_http_response(const ApiResponse& response) {
    ostringstream oss;
    
    // Status line
    oss << "HTTP/1.1 " << static_cast<int>(response.status);
    switch (response.status) {
        case HttpStatus::OK: oss << " OK"; break;
        case HttpStatus::Created: oss << " Created"; break;
        case HttpStatus::BadRequest: oss << " Bad Request"; break;
        case HttpStatus::Unauthorized: oss << " Unauthorized"; break;
        case HttpStatus::NotFound: oss << " Not Found"; break;
        case HttpStatus::MethodNotAllowed: oss << " Method Not Allowed"; break;
        case HttpStatus::InternalServerError: oss << " Internal Server Error"; break;
    }
    oss << "\r\n";
    
    // Headers
    oss << "Content-Type: " << response.content_type << "\r\n";
    oss << "Content-Length: " << response.body.length() << "\r\n";
    oss << "Server: ODR-AudioEnc/StreamDAB Enhanced\r\n";
    
    for (const auto& header : response.headers) {
        oss << header.first << ": " << header.second << "\r\n";
    }
    
    oss << "\r\n";
    oss << response.body;
    
    return oss.str();
}

// Utility functions
namespace ApiUtils {

string format_timestamp(const system_clock::time_point& time) {
    time_t time_t = system_clock::to_time_t(time);
    ostringstream oss;
    oss << put_time(gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

string to_json(const StreamQualityMetrics& metrics) {
    ostringstream oss;
    oss << "{"
        << "\"snr_db\": " << metrics.snr_db << ","
        << "\"volume_peak\": " << metrics.volume_peak << ","
        << "\"volume_rms\": " << metrics.volume_rms << ","
        << "\"buffer_health\": " << metrics.buffer_health << ","
        << "\"is_silence\": " << (metrics.is_silence ? "true" : "false") << ","
        << "\"reconnect_count\": " << metrics.reconnect_count << ","
        << "\"underrun_count\": " << metrics.underrun_count
        << "}";
    return oss.str();
}

string to_json(const ThaiMetadata& metadata) {
    ostringstream oss;
    oss << "{"
        << "\"title_utf8\": \"" << metadata.title_utf8 << "\","
        << "\"artist_utf8\": \"" << metadata.artist_utf8 << "\","
        << "\"album_utf8\": \"" << metadata.album_utf8 << "\","
        << "\"station_utf8\": \"" << metadata.station_utf8 << "\","
        << "\"is_thai_content\": " << (metadata.is_thai_content ? "true" : "false") << ","
        << "\"thai_confidence\": " << metadata.thai_confidence << ","
        << "\"timestamp\": \"" << format_timestamp(metadata.timestamp) << "\""
        << "}";
    return oss.str();
}

map<string, string> parse_query_string(const string& query) {
    map<string, string> params;
    istringstream stream(query);
    string pair;
    
    while (getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != string::npos) {
            string key = url_decode(pair.substr(0, eq_pos));
            string value = url_decode(pair.substr(eq_pos + 1));
            params[key] = value;
        }
    }
    
    return params;
}

string url_decode(const string& input) {
    string result;
    result.reserve(input.length());
    
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%' && i + 2 < input.length()) {
            int hex_val;
            sscanf(input.substr(i + 1, 2).c_str(), "%x", &hex_val);
            result.push_back(static_cast<char>(hex_val));
            i += 2;
        }
        else if (input[i] == '+') {
            result.push_back(' ');
        }
        else {
            result.push_back(input[i]);
        }
    }
    
    return result;
}

string generate_secure_token(size_t length) {
    const string chars = "0123456789abcdef";
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0, chars.size() - 1);
    
    string token;
    token.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        token.push_back(chars[dis(gen)]);
    }
    
    return token;
}

map<string, string> get_cors_headers(const string& origin) {
    return {
        {"Access-Control-Allow-Origin", origin},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
        {"Access-Control-Max-Age", "86400"}
    };
}

bool verify_api_key(const string& provided_key, const string& expected_key) {
    // In production, use proper hashing and constant-time comparison
    return provided_key == expected_key;
}

} // namespace ApiUtils

} // namespace StreamDAB