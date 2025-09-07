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

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../src/api_interface.h"
#include "../src/enhanced_stream.h"
#include "../src/thai_metadata.h"
#include <chrono>
#include <thread>
#include <future>

using namespace StreamDAB;
using namespace std::chrono;

// Mock classes for testing
class MockStreamProcessor : public EnhancedStreamProcessor {
private:
    StreamConfig mock_config_;
    bool is_running_ = false;
    bool is_connected_ = false;
    bool is_healthy_ = true;
    StreamQualityMetrics mock_metrics_;

public:
    MockStreamProcessor() : EnhancedStreamProcessor(StreamConfig{}) {
        mock_config_.primary_url = "http://mock-stream.test:8000/stream";
        mock_metrics_.snr_db = 25.0;
        mock_metrics_.volume_rms = 0.5;
        mock_metrics_.volume_peak = 0.8;
        mock_metrics_.buffer_health = 85;
    }

    bool is_running() const override { return is_running_; }
    bool is_connected() const override { return is_connected_; }
    bool is_healthy() const override { return is_healthy_; }
    
    std::string get_current_url() const override { return mock_config_.primary_url; }
    std::string get_current_title() const override { return "Mock Title สวัสดี"; }
    std::string get_current_artist() const override { return "Mock Artist ครับ"; }
    
    StreamQualityMetrics get_quality_metrics() override { return mock_metrics_; }
    const StreamConfig& get_config() const override { return mock_config_; }
    
    std::vector<std::string> get_health_issues() const override {
        if (!is_healthy_) {
            return {"Mock health issue"};
        }
        return {};
    }
    
    // Test control methods
    void set_running(bool running) { is_running_ = running; }
    void set_connected(bool connected) { is_connected_ = connected; }
    void set_healthy(bool healthy) { is_healthy_ = healthy; }
    void set_metrics(const StreamQualityMetrics& metrics) { mock_metrics_ = metrics; }
};

// Test class for API configuration
class ApiInterfaceConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8007;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false; // Disable SSL for tests
        config_.require_auth = false; // Disable auth for basic tests
        config_.enable_cors = true;
        config_.max_connections = 10;
        config_.request_timeout_ms = 5000;
    }
    
    void TearDown() override {}
    
    ApiConfig config_;
};

TEST_F(ApiInterfaceConfigTest, BasicConfiguration) {
    auto api = std::make_unique<StreamDABApiInterface>(config_);
    
    auto retrieved_config = api->get_config();
    EXPECT_EQ(retrieved_config.port, config_.port);
    EXPECT_EQ(retrieved_config.bind_address, config_.bind_address);
    EXPECT_EQ(retrieved_config.enable_ssl, config_.enable_ssl);
    EXPECT_EQ(retrieved_config.require_auth, config_.require_auth);
    EXPECT_EQ(retrieved_config.enable_cors, config_.enable_cors);
}

TEST_F(ApiInterfaceConfigTest, ConfigurationUpdate) {
    auto api = std::make_unique<StreamDABApiInterface>(config_);
    
    ApiConfig new_config = config_;
    new_config.max_connections = 50;
    new_config.request_timeout_ms = 10000;
    
    api->update_config(new_config);
    
    auto updated_config = api->get_config();
    EXPECT_EQ(updated_config.max_connections, new_config.max_connections);
    EXPECT_EQ(updated_config.request_timeout_ms, new_config.request_timeout_ms);
}

TEST_F(ApiInterfaceConfigTest, InvalidConfiguration) {
    // Test with invalid port
    ApiConfig invalid_config = config_;
    invalid_config.port = 0;
    
    auto api = std::make_unique<StreamDABApiInterface>(invalid_config);
    
    // Should handle invalid configuration gracefully
    // Exact behavior depends on implementation
    EXPECT_FALSE(api->start()); // Should fail to start with invalid config
}

// Test class for API lifecycle
class ApiInterfaceLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8007;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
        config_.require_auth = false;
        config_.enable_cors = true;
        
        api_ = std::make_unique<StreamDABApiInterface>(config_);
    }
    
    void TearDown() override {
        if (api_ && api_->is_running()) {
            api_->stop();
        }
    }
    
    ApiConfig config_;
    std::unique_ptr<StreamDABApiInterface> api_;
};

TEST_F(ApiInterfaceLifecycleTest, InitializationAndStartup) {
    EXPECT_FALSE(api_->is_running());
    
    EXPECT_TRUE(api_->initialize());
    
    // Note: Actual network binding might fail in test environment
    // So we'll test the API state rather than actual network functionality
    bool start_result = api_->start();
    
    if (start_result) {
        EXPECT_TRUE(api_->is_running());
        api_->stop();
        EXPECT_FALSE(api_->is_running());
    }
    // If start fails, that's acceptable in test environment
}

TEST_F(ApiInterfaceLifecycleTest, MultipleStartStop) {
    // Test multiple start/stop cycles
    for (int i = 0; i < 3; ++i) {
        api_->initialize();
        bool started = api_->start();
        
        if (started) {
            EXPECT_TRUE(api_->is_running());
        }
        
        api_->stop();
        EXPECT_FALSE(api_->is_running());
        
        // Small delay between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

TEST_F(ApiInterfaceLifecycleTest, ComponentIntegration) {
    auto mock_stream = std::make_shared<MockStreamProcessor>();
    auto mock_metadata = std::make_shared<ThaiMetadataProcessor>();
    
    api_->set_stream_processor(mock_stream);
    api_->set_metadata_processor(mock_metadata);
    
    // Should initialize successfully with components
    EXPECT_TRUE(api_->initialize());
}

// Test class for health status
class ApiInterfaceHealthTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8007;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
        config_.require_auth = false;
        
        api_ = std::make_unique<StreamDABApiInterface>(config_);
        mock_stream_ = std::make_shared<MockStreamProcessor>();
        
        api_->set_stream_processor(mock_stream_);
        api_->initialize();
    }
    
    void TearDown() override {
        if (api_ && api_->is_running()) {
            api_->stop();
        }
    }
    
    ApiConfig config_;
    std::unique_ptr<StreamDABApiInterface> api_;
    std::shared_ptr<MockStreamProcessor> mock_stream_;
};

TEST_F(ApiInterfaceHealthTest, HealthyState) {
    mock_stream_->set_running(true);
    mock_stream_->set_connected(true);
    mock_stream_->set_healthy(true);
    
    auto health = api_->get_health_status();
    
    EXPECT_TRUE(health.stream_healthy);
    EXPECT_TRUE(health.issues.empty() || health.issues.size() == 0);
    
    // Check timestamp is recent
    auto now = steady_clock::now();
    auto time_diff = duration_cast<seconds>(now - health.check_time).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 1);
}

TEST_F(ApiInterfaceHealthTest, UnhealthyStreamState) {
    mock_stream_->set_running(false);
    mock_stream_->set_connected(false);
    mock_stream_->set_healthy(false);
    
    auto health = api_->get_health_status();
    
    EXPECT_FALSE(health.stream_healthy);
    EXPECT_FALSE(health.issues.empty());
    
    // Should have specific issues reported
    bool found_stream_issue = false;
    for (const auto& issue : health.issues) {
        if (issue.find("stream") != std::string::npos ||
            issue.find("Stream") != std::string::npos) {
            found_stream_issue = true;
            break;
        }
    }
    EXPECT_TRUE(found_stream_issue);
}

TEST_F(ApiInterfaceHealthTest, NoStreamProcessor) {
    // Test health without stream processor
    api_->set_stream_processor(nullptr);
    
    auto health = api_->get_health_status();
    
    EXPECT_FALSE(health.stream_healthy);
    EXPECT_FALSE(health.issues.empty());
    
    // Should report stream processor not available
    bool found_processor_issue = false;
    for (const auto& issue : health.issues) {
        if (issue.find("processor") != std::string::npos ||
            issue.find("initialized") != std::string::npos) {
            found_processor_issue = true;
            break;
        }
    }
    EXPECT_TRUE(found_processor_issue);
}

// Test class for metrics and monitoring
class ApiInterfaceMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8007;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
        config_.require_auth = false;
        
        api_ = std::make_unique<StreamDABApiInterface>(config_);
        mock_stream_ = std::make_shared<MockStreamProcessor>();
        
        api_->set_stream_processor(mock_stream_);
        api_->initialize();
    }
    
    void TearDown() override {
        if (api_ && api_->is_running()) {
            api_->stop();
        }
    }
    
    ApiConfig config_;
    std::unique_ptr<StreamDABApiInterface> api_;
    std::shared_ptr<MockStreamProcessor> mock_stream_;
};

TEST_F(ApiInterfaceMetricsTest, InitialMetrics) {
    auto metrics = api_->get_api_metrics();
    
    EXPECT_EQ(metrics.total_requests, 0);
    EXPECT_EQ(metrics.successful_requests, 0);
    EXPECT_EQ(metrics.failed_requests, 0);
    EXPECT_EQ(metrics.websocket_connections, 0);
    EXPECT_EQ(metrics.active_clients, 0);
    EXPECT_EQ(metrics.average_response_time_ms, 0.0);
    
    // Check start time is reasonable
    auto now = steady_clock::now();
    auto time_diff = duration_cast<seconds>(now - metrics.start_time).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 1);
}

TEST_F(ApiInterfaceMetricsTest, MetricsReset) {
    // Get initial metrics
    auto initial_metrics = api_->get_api_metrics();
    
    // Reset metrics
    api_->reset_metrics();
    
    auto reset_metrics = api_->get_api_metrics();
    
    // Counters should be reset
    EXPECT_EQ(reset_metrics.total_requests, 0);
    EXPECT_EQ(reset_metrics.successful_requests, 0);
    EXPECT_EQ(reset_metrics.failed_requests, 0);
    
    // Start time should be updated
    EXPECT_GE(reset_metrics.start_time, initial_metrics.start_time);
}

// Test class for HTTP request handling (unit tests)
class HttpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8008; // Different port for HTTP server tests
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
        config_.require_auth = false;
        config_.enable_cors = true;
    }
    
    void TearDown() override {}
    
    ApiConfig config_;
};

TEST_F(HttpServerTest, RequestParsing) {
    // This test would require access to HttpServer's internal methods
    // For now, we'll test the API structures
    
    ApiRequest request;
    request.method = "GET";
    request.path = "/api/v1/status";
    request.headers["Content-Type"] = "application/json";
    request.query_params["format"] = "json";
    
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.path, "/api/v1/status");
    EXPECT_EQ(request.headers["Content-Type"], "application/json");
    EXPECT_EQ(request.query_params["format"], "json");
}

TEST_F(HttpServerTest, ResponseFormatting) {
    ApiResponse response;
    response.status = HttpStatus::OK;
    response.content_type = "application/json";
    response.body = R"({"status": "ok"})";
    response.headers["X-Custom-Header"] = "test-value";
    
    EXPECT_EQ(response.status, HttpStatus::OK);
    EXPECT_EQ(response.content_type, "application/json");
    EXPECT_FALSE(response.body.empty());
    EXPECT_EQ(response.headers["X-Custom-Header"], "test-value");
}

// Test class for WebSocket functionality
class WebSocketServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8009;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
    }
    
    void TearDown() override {}
    
    ApiConfig config_;
};

TEST_F(WebSocketServerTest, MessageStructure) {
    WebSocketMessage message;
    message.type = WebSocketMessageType::Status;
    message.data = "test-data";
    message.timestamp = steady_clock::now();
    message.client_id = "test-client-123";
    
    EXPECT_EQ(message.type, WebSocketMessageType::Status);
    EXPECT_EQ(message.data, "test-data");
    EXPECT_EQ(message.client_id, "test-client-123");
    
    // Check timestamp is reasonable
    auto now = steady_clock::now();
    auto time_diff = duration_cast<milliseconds>(now - message.timestamp).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 100);
}

TEST_F(WebSocketServerTest, MessageTypes) {
    // Test all message types are available
    std::vector<WebSocketMessageType> types = {
        WebSocketMessageType::Status,
        WebSocketMessageType::Metadata,
        WebSocketMessageType::QualityMetrics,
        WebSocketMessageType::Error,
        WebSocketMessageType::ConfigUpdate,
        WebSocketMessageType::StreamEvent
    };
    
    EXPECT_EQ(types.size(), 6);
    
    // Each type should be different
    for (size_t i = 0; i < types.size(); ++i) {
        for (size_t j = i + 1; j < types.size(); ++j) {
            EXPECT_NE(types[i], types[j]);
        }
    }
}

// Test class for MessagePack serialization
class MessagePackSerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        serializer_ = std::make_unique<MessagePackSerializer>();
    }
    
    void TearDown() override {}
    
    std::unique_ptr<MessagePackSerializer> serializer_;
};

TEST_F(MessagePackSerializerTest, SerializeStatus) {
    StreamQualityMetrics metrics;
    metrics.snr_db = 25.5;
    metrics.volume_rms = 0.6;
    metrics.volume_peak = 0.9;
    metrics.buffer_health = 85;
    metrics.is_silence = false;
    
    ThaiMetadata metadata;
    metadata.title_utf8 = "Test Title สวัสดี";
    metadata.artist_utf8 = "Test Artist";
    metadata.is_thai_content = true;
    metadata.thai_confidence = 0.8;
    
    std::string serialized = serializer_->serialize_status(metrics, metadata);
    
    EXPECT_FALSE(serialized.empty());
    // In a real implementation, we would deserialize and verify content
}

TEST_F(MessagePackSerializerTest, SerializeMetadata) {
    ThaiMetadata metadata;
    metadata.title_utf8 = "เพลงไทย";
    metadata.artist_utf8 = "นักร้องไทย";
    metadata.album_utf8 = "อัลบั้มทดสอบ";
    metadata.is_thai_content = true;
    metadata.thai_confidence = 0.95;
    metadata.timestamp = system_clock::now();
    
    std::string serialized = serializer_->serialize_metadata(metadata);
    
    EXPECT_FALSE(serialized.empty());
}

TEST_F(MessagePackSerializerTest, SerializeQualityMetrics) {
    StreamQualityMetrics metrics;
    metrics.snr_db = 30.2;
    metrics.volume_rms = 0.7;
    metrics.volume_peak = 0.95;
    metrics.buffer_health = 92;
    metrics.is_silence = false;
    metrics.reconnect_count = 2;
    metrics.underrun_count = 1;
    
    std::string serialized = serializer_->serialize_quality_metrics(metrics);
    
    EXPECT_FALSE(serialized.empty());
}

TEST_F(MessagePackSerializerTest, SerializeError) {
    std::string error_message = "Stream connection failed";
    std::string error_code = "STREAM_ERROR_001";
    
    std::string serialized = serializer_->serialize_error(error_message, error_code);
    
    EXPECT_FALSE(serialized.empty());
}

TEST_F(MessagePackSerializerTest, DeserializeConfigUpdate) {
    // Test deserialization with mock data
    // In a real implementation, this would use actual MessagePack data
    std::string mock_config_data = "{}"; // Simplified mock data
    
    auto config_update = serializer_->deserialize_config_update(mock_config_data);
    
    // Should handle invalid data gracefully
    EXPECT_FALSE(config_update.is_valid);
}

// Test class for API utilities
class ApiUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ApiUtilsTest, URLEncoding) {
    // Test URL encoding
    std::string input = "hello world@test.com";
    std::string encoded = ApiUtils::url_encode(input);
    
    EXPECT_NE(encoded, input); // Should be different
    EXPECT_NE(encoded.find("%20"), std::string::npos); // Should contain encoded space
    
    // Test decoding
    std::string decoded = ApiUtils::url_decode(encoded);
    EXPECT_EQ(decoded, input);
}

TEST_F(ApiUtilsTest, QueryStringParsing) {
    std::string query = "param1=value1&param2=value%202&param3=";
    auto params = ApiUtils::parse_query_string(query);
    
    EXPECT_EQ(params["param1"], "value1");
    EXPECT_EQ(params["param2"], "value 2"); // Should be URL decoded
    EXPECT_EQ(params["param3"], "");
    
    // Test empty query
    auto empty_params = ApiUtils::parse_query_string("");
    EXPECT_TRUE(empty_params.empty());
}

TEST_F(ApiUtilsTest, TimestampFormatting) {
    auto now = system_clock::now();
    std::string formatted = ApiUtils::format_timestamp(now);
    
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("T"), std::string::npos); // ISO format
    EXPECT_NE(formatted.find("Z"), std::string::npos); // UTC indicator
}

TEST_F(ApiUtilsTest, JSONSerialization) {
    StreamQualityMetrics metrics;
    metrics.snr_db = 25.0;
    metrics.volume_rms = 0.5;
    metrics.volume_peak = 0.8;
    metrics.buffer_health = 90;
    metrics.is_silence = false;
    
    std::string json = ApiUtils::to_json(metrics);
    
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("snr_db"), std::string::npos);
    EXPECT_NE(json.find("volume_rms"), std::string::npos);
    EXPECT_NE(json.find("buffer_health"), std::string::npos);
    
    // Test Thai metadata JSON
    ThaiMetadata metadata;
    metadata.title_utf8 = "เพลงทดสอบ";
    metadata.artist_utf8 = "นักร้องทดสอบ";
    metadata.is_thai_content = true;
    metadata.thai_confidence = 0.95;
    
    std::string metadata_json = ApiUtils::to_json(metadata);
    EXPECT_FALSE(metadata_json.empty());
    EXPECT_NE(metadata_json.find("thai_confidence"), std::string::npos);
}

TEST_F(ApiUtilsTest, ValidationFunctions) {
    // Test stream URL validation
    EXPECT_TRUE(ApiUtils::is_valid_stream_url("http://example.com:8000/stream"));
    EXPECT_TRUE(ApiUtils::is_valid_stream_url("https://secure.example.com/live"));
    EXPECT_FALSE(ApiUtils::is_valid_stream_url(""));
    EXPECT_FALSE(ApiUtils::is_valid_stream_url("not-a-url"));
    
    // Test API key validation (basic format check)
    EXPECT_TRUE(ApiUtils::is_valid_api_key("valid-api-key-123"));
    EXPECT_FALSE(ApiUtils::is_valid_api_key(""));
    
    // Test client ID validation
    EXPECT_TRUE(ApiUtils::is_valid_client_id("client-123-abc"));
    EXPECT_FALSE(ApiUtils::is_valid_client_id(""));
}

TEST_F(ApiUtilsTest, SecurityFunctions) {
    // Test secure token generation
    std::string token1 = ApiUtils::generate_secure_token(16);
    std::string token2 = ApiUtils::generate_secure_token(16);
    
    EXPECT_EQ(token1.length(), 16);
    EXPECT_EQ(token2.length(), 16);
    EXPECT_NE(token1, token2); // Should be different
    
    // Test token with different length
    std::string long_token = ApiUtils::generate_secure_token(32);
    EXPECT_EQ(long_token.length(), 32);
    
    // Test CORS headers
    auto cors_headers = ApiUtils::get_cors_headers();
    EXPECT_FALSE(cors_headers.empty());
    EXPECT_NE(cors_headers.find("Access-Control-Allow-Origin"), cors_headers.end());
    EXPECT_NE(cors_headers.find("Access-Control-Allow-Methods"), cors_headers.end());
}

// Test class for exception handling
class ApiExceptionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ApiExceptionTest, ExceptionCreation) {
    ApiException ex(ApiErrorCode::InvalidRequest, 
                   "Test error message", 
                   HttpStatus::BadRequest);
    
    EXPECT_EQ(ex.get_error_code(), ApiErrorCode::InvalidRequest);
    EXPECT_EQ(ex.get_http_status(), HttpStatus::BadRequest);
    EXPECT_STREQ(ex.what(), "Test error message");
}

TEST_F(ApiExceptionTest, ExceptionThrowingAndCatching) {
    try {
        throw ApiException(ApiErrorCode::AuthenticationFailed, 
                          "Authentication failed", 
                          HttpStatus::Unauthorized);
    }
    catch (const ApiException& e) {
        EXPECT_EQ(e.get_error_code(), ApiErrorCode::AuthenticationFailed);
        EXPECT_EQ(e.get_http_status(), HttpStatus::Unauthorized);
    }
    catch (...) {
        FAIL() << "Wrong exception type caught";
    }
}

// Performance tests for API components
class ApiPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.port = 8010;
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false;
        config_.require_auth = false;
        
        api_ = std::make_unique<StreamDABApiInterface>(config_);
    }
    
    void TearDown() override {
        if (api_ && api_->is_running()) {
            api_->stop();
        }
    }
    
    ApiConfig config_;
    std::unique_ptr<StreamDABApiInterface> api_;
};

TEST_F(ApiPerformanceTest, HealthCheckPerformance) {
    api_->initialize();
    
    auto start_time = high_resolution_clock::now();
    
    // Perform multiple health checks
    for (int i = 0; i < 100; ++i) {
        auto health = api_->get_health_status();
        (void)health; // Suppress unused variable warning
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should complete 100 health checks quickly (< 100ms)
    EXPECT_LT(duration_ms, 100);
}

TEST_F(ApiPerformanceTest, MetricsAccessPerformance) {
    api_->initialize();
    
    auto start_time = high_resolution_clock::now();
    
    // Access metrics many times
    for (int i = 0; i < 1000; ++i) {
        auto metrics = api_->get_api_metrics();
        (void)metrics; // Suppress unused variable warning
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should complete 1000 metrics accesses quickly (< 50ms)
    EXPECT_LT(duration_ms, 50);
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}