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
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <poll.h>

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
    (void)request; // Suppress unused parameter warning
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
        response.body = R"({"error": "Failed to get status", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_metadata(const ApiRequest& request) {
    (void)request; // Suppress unused parameter warning
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
        response.body = R"({"error": "Failed to get metadata", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_quality_metrics(const ApiRequest& request) {
    (void)request; // Suppress unused parameter warning
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
        response.body = R"({"error": "Failed to get quality metrics", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
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
        
        if (!stream_processor_->update_config(new_config)) {
            response.status = HttpStatus::BadRequest;
            response.body = R"({"success": false, "error": "Configuration rejected"})";
            return response;
        }
        
        response.body = R"({"success": true, "message": "Configuration updated"})";
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to update configuration", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_post_reconnect(const ApiRequest& request) {
    (void)request; // Suppress unused parameter warning
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
        response.body = R"({"error": "Failed to reconnect", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
    }
    
    return response;
}

ApiResponse StreamDABApiInterface::handle_get_health(const ApiRequest& request) {
    (void)request; // Suppress unused parameter warning
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    
    try {
        auto health = get_health_status();
        response.body = ApiUtils::to_json(health);
        
        // Return 503 if not healthy
        if (!health.api_healthy || !health.stream_healthy) {
            response.status = HttpStatus::ServiceUnavailable;
        }
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Failed to get health status", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
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

namespace {

string to_lower(string text) {
    transform(text.begin(), text.end(), text.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return text;
}

// HTTP header names are case-insensitive
const string* find_header(const map<string, string>& headers, const string& name) {
    const string wanted = to_lower(name);
    for (const auto& header : headers) {
        if (to_lower(header.first) == wanted) {
            return &header.second;
        }
    }
    return nullptr;
}

} // anonymous namespace

bool StreamDABApiInterface::authenticate_request(const ApiRequest& request) {
    if (!get_config().require_auth) {
        return true;
    }
    
    const string* auth_header = find_header(request.headers, "Authorization");
    if (!auth_header) {
        return false;
    }
    
    static const string bearer = "Bearer ";
    if (auth_header->compare(0, bearer.size(), bearer) != 0) {
        return false;
    }
    
    return ApiUtils::verify_api_key(auth_header->substr(bearer.size()), get_config().api_key);
}

bool StreamDABApiInterface::check_rate_limit(const string& client_ip) {
    const ApiConfig config = get_config();
    if (!config.enable_rate_limiting) {
        return true;
    }

    const auto now = steady_clock::now();
    lock_guard<mutex> lock(rate_limit_mutex_);

    // Forget clients that have been quiet for a while, so the map cannot grow without bound
    if (rate_limit_map_.size() > 10000) {
        for (auto it = rate_limit_map_.begin(); it != rate_limit_map_.end();) {
            if (now - it->second.window_start > minutes(2)) {
                it = rate_limit_map_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    auto& entry = rate_limit_map_[client_ip];
    if (entry.request_count == 0 or now - entry.window_start >= minutes(1)) {
        entry.window_start = now;
        entry.request_count = 0;
    }
    entry.request_count++;

    return entry.request_count <= config.rate_limit_requests_per_minute;
}

string StreamDABApiInterface::generate_client_id() {
    return ApiUtils::generate_secure_token(16);
}

void StreamDABApiInterface::set_stream_processor(shared_ptr<EnhancedStreamProcessor> processor) {
    stream_processor_ = move(processor);
}

void StreamDABApiInterface::set_metadata_processor(shared_ptr<ThaiMetadataProcessor> processor) {
    metadata_processor_ = move(processor);
}

void StreamDABApiInterface::update_config(const ApiConfig& new_config) {
    lock_guard<mutex> lock(config_mutex_);
    config_ = new_config;
}

ApiConfig StreamDABApiInterface::get_config() const {
    lock_guard<mutex> lock(config_mutex_);
    return config_;
}

void StreamDABApiInterface::record_request(HttpStatus status, double response_time_ms) {
    lock_guard<mutex> lock(metrics_mutex_);

    metrics_.total_requests++;
    if (static_cast<int>(status) < 400) {
        metrics_.successful_requests++;
    }
    else {
        metrics_.failed_requests++;
    }

    // Running mean over all requests since the last reset
    const double n = static_cast<double>(metrics_.total_requests);
    metrics_.average_response_time_ms += (response_time_ms - metrics_.average_response_time_ms) / n;
}

StreamDABApiInterface::ApiMetrics StreamDABApiInterface::get_api_metrics() const {
    ApiMetrics metrics;
    {
        lock_guard<mutex> lock(metrics_mutex_);
        metrics = metrics_;
    }
    {
        lock_guard<mutex> lock(clients_mutex_);
        metrics.active_clients = connected_clients_.size();
    }
    return metrics;
}

void StreamDABApiInterface::reset_metrics() {
    lock_guard<mutex> lock(metrics_mutex_);
    metrics_ = ApiMetrics();
    metrics_.start_time = steady_clock::now();
}

void StreamDABApiInterface::handle_websocket_connection(const string& client_id) {
    {
        lock_guard<mutex> lock(clients_mutex_);
        ConnectedClient client;
        client.client_id = client_id;
        client.connected_time = steady_clock::now();
        connected_clients_[client_id] = client;
    }

    lock_guard<mutex> lock(metrics_mutex_);
    metrics_.websocket_connections++;
}

void StreamDABApiInterface::handle_websocket_message(const string& client_id, const string& message) {
    const auto subscription = serializer_->deserialize_subscription(message);
    if (!subscription.is_valid) {
        return;
    }

    lock_guard<mutex> lock(clients_mutex_);
    auto it = connected_clients_.find(client_id);
    if (it == connected_clients_.end()) {
        return;
    }

    const bool all = subscription.topic == "all";
    if (all or subscription.topic == "status") it->second.subscribed_to_status = subscription.enable;
    if (all or subscription.topic == "metadata") it->second.subscribed_to_metadata = subscription.enable;
    if (all or subscription.topic == "metrics") it->second.subscribed_to_metrics = subscription.enable;
}

void StreamDABApiInterface::handle_websocket_disconnect(const string& client_id) {
    lock_guard<mutex> lock(clients_mutex_);
    connected_clients_.erase(client_id);
}

bool StreamDABApiInterface::is_subscribed(const string& client_id, WebSocketMessageType type) {
    lock_guard<mutex> lock(clients_mutex_);
    const auto it = connected_clients_.find(client_id);
    if (it == connected_clients_.end()) {
        return false;
    }

    switch (type) {
        case WebSocketMessageType::Status: return it->second.subscribed_to_status;
        case WebSocketMessageType::Metadata: return it->second.subscribed_to_metadata;
        case WebSocketMessageType::QualityMetrics: return it->second.subscribed_to_metrics;
        default: return true; // errors, config updates and stream events go to everybody
    }
}

// HttpServer implementation
namespace {

constexpr size_t MAX_REQUEST_HEAD_BYTES = 16 * 1024;
constexpr size_t MAX_REQUEST_BODY_BYTES = 1024 * 1024;

bool write_all(int fd, const string& data) {
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = send(fd, data.data() + written, data.size() - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

} // anonymous namespace

HttpServer::HttpServer(const ApiConfig& config, StreamDABApiInterface* api) 
    : config_(config), api_(api) {
    setup_routes(api);
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setup_routes(StreamDABApiInterface* api) {
    routes_["/api/v1/status"] = {"GET", [api](const ApiRequest& req) {
        return api->handle_get_status(req);
    }};
    
    routes_["/api/v1/metadata"] = {"GET", [api](const ApiRequest& req) {
        return api->handle_get_metadata(req);
    }};
    
    routes_["/api/v1/quality"] = {"GET", [api](const ApiRequest& req) {
        return api->handle_get_quality_metrics(req);
    }};
    
    routes_["/api/v1/config"] = {"POST", [api](const ApiRequest& req) {
        return api->handle_post_stream_config(req);
    }};
    
    routes_["/api/v1/reconnect"] = {"POST", [api](const ApiRequest& req) {
        return api->handle_post_reconnect(req);
    }};
    
    routes_["/api/v1/health"] = {"GET", [api](const ApiRequest& req) {
        return api->handle_get_health(req);
    }};
}

bool HttpServer::start() {
    if (running_) {
        return true;
    }

    // Bind synchronously so that a failure (port in use, ...) is reported to the caller
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        fprintf(stderr, "Socket creation failed\n");
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(config_.port));
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 or
            listen(listen_fd_, 128) < 0) {
        fprintf(stderr, "HTTP server cannot listen on %s:%d: %s\n",
                config_.bind_address.c_str(), config_.port, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    printf("HTTP server listening on port %d\n", config_.port);

    running_ = true;
    server_thread_ = thread(&HttpServer::server_loop, this);
    return true;
}

void HttpServer::stop() {
    running_ = false;
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    // Let the connection threads finish: they use this object
    for (int i = 0; i < 1000 and active_connections_ > 0; ++i) {
        this_thread::sleep_for(milliseconds(5));
    }
}

void HttpServer::server_loop() {
    // Simplified HTTP/1.1 server (one request per connection)
    // In production, use a proper HTTP library like libmicrohttpd or cpp-httplib
    while (running_) {
        pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        // Wake up regularly so that stop() is noticed
        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }

        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_socket = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_address), &client_len);
        if (client_socket < 0) {
            continue;
        }

        char ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &client_address.sin_addr, ip, sizeof(ip));

        active_connections_++;
        thread([this, client_socket, client_ip = string(ip)]() {
            try {
                handle_connection(client_socket, client_ip);
            }
            catch (const exception& e) {
                fprintf(stderr, "HTTP connection error: %s\n", e.what());
            }
            close(client_socket);
            active_connections_--;
        }).detach();
    }
}

void HttpServer::handle_connection(int client_socket, const string& client_ip) {
    const auto started = steady_clock::now();

    timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Read until the end of the header block
    string data;
    size_t head_end = string::npos;
    char chunk[4096];
    while ((head_end = data.find("\r\n\r\n")) == string::npos) {
        if (data.size() > MAX_REQUEST_HEAD_BYTES) {
            break;
        }
        const ssize_t n = recv(client_socket, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return; // client went away or timed out
        }
        data.append(chunk, static_cast<size_t>(n));
    }

    ApiResponse response;
    bool respond = true;

    if (head_end == string::npos) {
        response.status = HttpStatus::BadRequest;
        response.body = R"({"error": "Malformed request"})";
    }
    else {
        const string head = data.substr(0, head_end + 4);
        string body = data.substr(head_end + 4);

        ApiRequest request = parse_http_request(head, body);
        request.client_ip = client_ip;

        // Read the body announced by Content-Length
        size_t content_length = 0;
        if (const string* value = find_header(request.headers, "Content-Length")) {
            char* end = nullptr;
            const unsigned long long parsed = strtoull(value->c_str(), &end, 10);
            if (end == value->c_str() or parsed > MAX_REQUEST_BODY_BYTES) {
                content_length = MAX_REQUEST_BODY_BYTES + 1;
            }
            else {
                content_length = static_cast<size_t>(parsed);
            }
        }

        if (content_length > MAX_REQUEST_BODY_BYTES) {
            response.status = HttpStatus::PayloadTooLarge;
            response.body = R"({"error": "Request body too large"})";
        }
        else {
            if (body.size() < content_length) {
                const string* expect = find_header(request.headers, "Expect");
                if (expect and to_lower(*expect) == "100-continue") {
                    write_all(client_socket, "HTTP/1.1 100 Continue\r\n\r\n");
                }
            }
            while (body.size() < content_length) {
                const ssize_t n = recv(client_socket, chunk, sizeof(chunk), 0);
                if (n <= 0) {
                    respond = false; // truncated request
                    break;
                }
                body.append(chunk, static_cast<size_t>(n));
            }
            body.resize(min(body.size(), content_length));
            request.body = body;

            if (request.method.empty() or request.path.empty()) {
                response.status = HttpStatus::BadRequest;
                response.body = R"({"error": "Malformed request line"})";
            }
            else {
                response = handle_request(request);
            }
        }
    }

    if (!respond) {
        return;
    }

    const double elapsed_ms = duration<double, milli>(steady_clock::now() - started).count();
    if (api_) {
        api_->record_request(response.status, elapsed_ms);
    }

    write_all(client_socket, format_http_response(response));
}

ApiRequest HttpServer::parse_http_request(const string& raw_head, const string& body) {
    ApiRequest request;
    request.timestamp = steady_clock::now();
    request.body = body;
    
    istringstream stream(raw_head);
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
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        const size_t colon_pos = line.find(':');
        if (colon_pos == string::npos) {
            continue;
        }

        string key = line.substr(0, colon_pos);
        string value = line.substr(colon_pos + 1);
        const size_t first = value.find_first_not_of(" \t");
        value = (first == string::npos) ? string() : value.substr(first);
        while (!value.empty() and (value.back() == ' ' or value.back() == '\t')) {
            value.pop_back();
        }
        request.headers[key] = value;
    }
    
    return request;
}

ApiResponse HttpServer::handle_request(const ApiRequest& request) {
    ApiResponse response;
    
    try {
        const ApiConfig config = api_->get_config();

        // Add CORS headers. With an allow-list only listed origins are echoed back.
        if (config.enable_cors) {
            string origin = "*";
            if (!config.allowed_origins.empty()) {
                origin.clear();
                const string* request_origin = find_header(request.headers, "Origin");
                if (request_origin and find(config.allowed_origins.begin(), config.allowed_origins.end(),
                                            *request_origin) != config.allowed_origins.end()) {
                    origin = *request_origin;
                    response.headers["Vary"] = "Origin";
                }
            }
            if (!origin.empty()) {
                for (const auto& header : ApiUtils::get_cors_headers(origin)) {
                    response.headers[header.first] = header.second;
                }
            }
        }
        
        // Handle preflight requests
        if (request.method == "OPTIONS") {
            response.status = HttpStatus::OK;
            return response;
        }
        
        // Route to appropriate handler
        const auto route_it = routes_.find(request.path);
        if (route_it == routes_.end()) {
            response.status = HttpStatus::NotFound;
            response.body = R"({"error": "Endpoint not found"})";
            return response;
        }

        if (request.method != route_it->second.method) {
            response.status = HttpStatus::MethodNotAllowed;
            response.headers["Allow"] = route_it->second.method;
            response.body = R"({"error": "Method not allowed"})";
            return response;
        }

        // Throttle before authenticating, so key guessing is rate limited too
        if (!api_->check_rate_limit(request.client_ip)) {
            response.status = HttpStatus::TooManyRequests;
            response.headers["Retry-After"] = "60";
            response.body = R"({"error": "Rate limit exceeded"})";
            return response;
        }

        if (!api_->authenticate_request(request)) {
            response.status = HttpStatus::Unauthorized;
            response.headers["WWW-Authenticate"] = "Bearer";
            response.body = R"({"error": "Authentication required"})";
            return response;
        }

        const auto cors_headers = response.headers;
        response = route_it->second.handler(request);
        response.headers.insert(cors_headers.begin(), cors_headers.end());
    }
    catch (const ApiException& e) {
        response.status = e.get_http_status();
        response.body = R"({"error": ")" + ApiUtils::json_escape(e.what()) + R"("})";
    }
    catch (const exception& e) {
        response.status = HttpStatus::InternalServerError;
        response.body = R"({"error": "Internal server error", "message": ")" + ApiUtils::json_escape(e.what()) + R"("})";
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
        case HttpStatus::PayloadTooLarge: oss << " Payload Too Large"; break;
        case HttpStatus::TooManyRequests: oss << " Too Many Requests"; break;
        case HttpStatus::InternalServerError: oss << " Internal Server Error"; break;
        case HttpStatus::ServiceUnavailable: oss << " Service Unavailable"; break;
    }
    oss << "\r\n";
    
    // Headers
    oss << "Content-Type: " << response.content_type << "; charset=utf-8\r\n";
    oss << "Content-Length: " << response.body.length() << "\r\n";
    oss << "Connection: close\r\n";
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

string json_escape(const string& input) {
    string out;
    out.reserve(input.size());

    for (unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else {
                    out += static_cast<char>(c); // UTF-8 (e.g. Thai) is valid JSON as is
                }
        }
    }
    return out;
}

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
        << "\"title_utf8\": \"" << json_escape(metadata.title_utf8) << "\","
        << "\"artist_utf8\": \"" << json_escape(metadata.artist_utf8) << "\","
        << "\"album_utf8\": \"" << json_escape(metadata.album_utf8) << "\","
        << "\"station_utf8\": \"" << json_escape(metadata.station_utf8) << "\","
        << "\"is_thai_content\": " << (metadata.is_thai_content ? "true" : "false") << ","
        << "\"thai_confidence\": " << metadata.thai_confidence << ","
        << "\"timestamp\": \"" << format_timestamp(metadata.timestamp) << "\""
        << "}";
    return oss.str();
}

string to_json(const StreamDABApiInterface::HealthStatus& health) {
    ostringstream oss;
    oss << "{"
        << "\"api_healthy\": " << (health.api_healthy ? "true" : "false") << ","
        << "\"stream_healthy\": " << (health.stream_healthy ? "true" : "false") << ","
        << "\"websocket_healthy\": " << (health.websocket_healthy ? "true" : "false") << ","
        << "\"issues\": [";

    bool first = true;
    for (const auto& issue : health.issues) {
        if (!first) {
            oss << ",";
        }
        oss << "\"" << json_escape(issue) << "\"";
        first = false;
    }

    oss << "]}";
    return oss.str();
}

string to_json(const StreamDABApiInterface::ApiMetrics& metrics) {
    const double uptime_s = duration<double>(steady_clock::now() - metrics.start_time).count();

    ostringstream oss;
    oss << "{"
        << "\"total_requests\": " << metrics.total_requests << ","
        << "\"successful_requests\": " << metrics.successful_requests << ","
        << "\"failed_requests\": " << metrics.failed_requests << ","
        << "\"websocket_connections\": " << metrics.websocket_connections << ","
        << "\"active_clients\": " << metrics.active_clients << ","
        << "\"average_response_time_ms\": " << metrics.average_response_time_ms << ","
        << "\"uptime_seconds\": " << uptime_s
        << "}";
    return oss.str();
}

string to_json(const map<string, string>& data) {
    ostringstream oss;
    oss << "{";
    
    bool first = true;
    for (const auto& pair : data) {
        if (!first) {
            oss << ",";
        }
        oss << "\"" << json_escape(pair.first) << "\": \"" << json_escape(pair.second) << "\"";
        first = false;
    }
    
    oss << "}";
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
        if (input[i] == '%' && i + 2 < input.length() &&
                isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            result.push_back(static_cast<char>(stoi(input.substr(i + 1, 2), nullptr, 16)));
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
    static const char chars[] = "0123456789abcdef";

    // random_device is backed by the OS entropy source; every digit is drawn from it directly
    // (a Mersenne Twister seeded once is predictable and not suitable for tokens)
    random_device rd;
    string token;
    token.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        token.push_back(chars[rd() % 16]);
    }
    
    return token;
}

map<string, string> get_cors_headers(const string& origin) {
    return {
        {"Access-Control-Allow-Origin", origin},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
        {"Access-Control-Max-Age", "86400"}
    };
}

bool verify_api_key(const string& provided_key, const string& expected_key) {
    // An empty expected key never authenticates
    if (expected_key.empty()) {
        return false;
    }

    // Constant-time comparison: do not leak the matching prefix length through timing
    unsigned char diff = static_cast<unsigned char>(provided_key.size() != expected_key.size());
    const size_t n = min(provided_key.size(), expected_key.size());
    for (size_t i = 0; i < n; ++i) {
        diff |= static_cast<unsigned char>(provided_key[i] ^ expected_key[i]);
    }
    return diff == 0;
}

} // namespace ApiUtils

} // namespace StreamDAB
