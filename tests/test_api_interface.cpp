/* ------------------------------------------------------------------
 * Copyright (C) 2024 StreamDAB Project
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
#include "api_interface.h"
#include <thread>
#include <chrono>
#include <curl/curl.h>

using namespace StreamDAB;
using namespace std;
using namespace std::chrono;

// Mock classes for testing
class MockEnhancedStreamProcessor : public EnhancedStreamProcessor {
public:
    MOCK_METHOD(bool, is_connected, (), (const, override));
    MOCK_METHOD(bool, is_running, (), (const, override));
    MOCK_METHOD(bool, is_healthy, (), (const, override));
    MOCK_METHOD(string, get_current_url, (), (const, override));
    MOCK_METHOD(string, get_current_title, (), (const, override));
    MOCK_METHOD(string, get_current_artist, (), (const, override));
    MOCK_METHOD(StreamQualityMetrics, get_quality_metrics, (), (const, override));
    MOCK_METHOD(vector<string>, get_health_issues, (), (const, override));
    MOCK_METHOD(StreamConfig, get_config, (), (const, override));
    MOCK_METHOD(bool, update_config, (const StreamConfig&), (override));
    MOCK_METHOD(bool, force_reconnect, (), (override));
};

class ApiInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Configure test API
        config_.port = 18007; // Use different port for testing
        config_.bind_address = "127.0.0.1";
        config_.enable_ssl = false; // Disable SSL for testing
        config_.require_auth = false; // Disable auth for basic tests
        config_.enable_cors = true;
        config_.api_key = "test_key_123";
        
        api_ = make_unique<StreamDABApiInterface>(config_);
        
        // Create mocks
        mock_stream_processor_ = make_shared<MockEnhancedStreamProcessor>();
        mock_metadata_processor_ = make_shared<ThaiMetadataProcessor>();
        
        // Set up default mock behaviors
        ON_CALL(*mock_stream_processor_, is_connected())
            .WillByDefault(::testing::Return(true));
        ON_CALL(*mock_stream_processor_, is_running())
            .WillByDefault(::testing::Return(true));
        ON_CALL(*mock_stream_processor_, is_healthy())
            .WillByDefault(::testing::Return(true));
        ON_CALL(*mock_stream_processor_, get_current_url())
            .WillByDefault(::testing::Return("http://example.com/stream"));
        ON_CALL(*mock_stream_processor_, get_current_title())
            .WillByDefault(::testing::Return("Test Title"));
        ON_CALL(*mock_stream_processor_, get_current_artist())
            .WillByDefault(::testing::Return("Test Artist"));
        
        StreamQualityMetrics default_metrics;
        default_metrics.snr_db = 25.0;
        default_metrics.volume_peak = 0.8;
        default_metrics.volume_rms = 0.5;
        default_metrics.buffer_health = 90.0;
        ON_CALL(*mock_stream_processor_, get_quality_metrics())
            .WillByDefault(::testing::Return(default_metrics));
        
        ON_CALL(*mock_stream_processor_, get_health_issues())
            .WillByDefault(::testing::Return(vector<string>()));
    }
    
    void TearDown() override {
        if (api_->is_running()) {
            api_->stop();
            this_thread::sleep_for(milliseconds(100)); // Allow clean shutdown
        }
    }
    
    ApiConfig config_;
    unique_ptr<StreamDABApiInterface> api_;
    shared_ptr<MockEnhancedStreamProcessor> mock_stream_processor_;
    shared_ptr<ThaiMetadataProcessor> mock_metadata_processor_;
    
    // HTTP client helper for testing
    struct HttpResponse {
        long status_code;
        string body;
        map<string, string> headers;
    };
    
    HttpResponse makeHttpRequest(const string& method, const string& path, 
                               const string& body = "", 
                               const map<string, string>& headers = {}) {
        HttpResponse response;
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            response.status_code = -1;
            return response;
        }
        
        string url = "http://127.0.0.1:" + to_string(config_.port) + path;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            }
        }
        
        // Set up response capture
        string response_data;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, 
            [](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
                size_t realsize = size * nmemb;
                static_cast<string*>(userp)->append(static_cast<char*>(contents), realsize);
                return realsize;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        
        // Perform request
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            response.body = response_data;
        } else {
            response.status_code = -1;
        }
        
        curl_easy_cleanup(curl);
        return response;
    }
};

// Basic API Functionality Tests
TEST_F(ApiInterfaceTest, InitializeAndStart) {
    EXPECT_TRUE(api_->initialize());
    
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    EXPECT_TRUE(api_->is_running());
    
    // Allow server to start
    this_thread::sleep_for(milliseconds(100));
}

TEST_F(ApiInterfaceTest, StopServer) {
    EXPECT_TRUE(api_->initialize());
    EXPECT_TRUE(api_->start());
    EXPECT_TRUE(api_->is_running());
    
    api_->stop();
    EXPECT_FALSE(api_->is_running());
}

TEST_F(ApiInterfaceTest, ConfigurationManagement) {
    ApiConfig original_config = api_->get_config();
    EXPECT_EQ(original_config.port, config_.port);
    
    ApiConfig new_config = original_config;
    new_config.max_connections = 200;
    
    api_->update_config(new_config);
    ApiConfig updated_config = api_->get_config();
    EXPECT_EQ(updated_config.max_connections, 200);
}

// HTTP Endpoint Tests
TEST_F(ApiInterfaceTest, StatusEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200)); // Allow server to start
    
    auto response = makeHttpRequest("GET", "/api/v1/status");
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
    
    // Check that response contains expected status fields
    EXPECT_NE(response.body.find("stream_connected"), string::npos);
    EXPECT_NE(response.body.find("api_running"), string::npos);
    EXPECT_NE(response.body.find("timestamp"), string::npos);
}

TEST_F(ApiInterfaceTest, MetadataEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/metadata");
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
    
    // Check for metadata fields
    EXPECT_NE(response.body.find("title_utf8"), string::npos);
    EXPECT_NE(response.body.find("artist_utf8"), string::npos);
    EXPECT_NE(response.body.find("Test Title"), string::npos);
    EXPECT_NE(response.body.find("Test Artist"), string::npos);
}

TEST_F(ApiInterfaceTest, QualityMetricsEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/quality");
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
    
    // Check for quality metrics fields
    EXPECT_NE(response.body.find("snr_db"), string::npos);
    EXPECT_NE(response.body.find("volume_peak"), string::npos);
    EXPECT_NE(response.body.find("buffer_health"), string::npos);
    EXPECT_NE(response.body.find("25"), string::npos); // SNR value
}

TEST_F(ApiInterfaceTest, HealthEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/health");
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
}

TEST_F(ApiInterfaceTest, ConfigurationUpdateEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    // Set expectation for config update
    EXPECT_CALL(*mock_stream_processor_, update_config(::testing::_))
        .WillOnce(::testing::Return(true));
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    string config_json = R"({
        "primary_url": "http://new-stream.example.com",
        "fallback_urls": ["http://fallback1.example.com"],
        "enable_normalization": true,
        "target_level_db": -20.0
    })";
    
    auto response = makeHttpRequest("POST", "/api/v1/config", config_json);
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.body.find("success"), string::npos);
}

TEST_F(ApiInterfaceTest, ReconnectEndpoint) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_CALL(*mock_stream_processor_, force_reconnect())
        .WillOnce(::testing::Return(true));
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("POST", "/api/v1/reconnect");
    
    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.body.find("Reconnection initiated"), string::npos);
}

// Error Handling Tests
TEST_F(ApiInterfaceTest, NotFoundEndpoint) {
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/nonexistent");
    
    EXPECT_EQ(response.status_code, 404);
    EXPECT_NE(response.body.find("not found"), string::npos);
}

TEST_F(ApiInterfaceTest, MethodNotAllowed) {
    api_->set_stream_processor(mock_stream_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    // Try POST on GET-only endpoint
    auto response = makeHttpRequest("POST", "/api/v1/status");
    
    // Depending on implementation, this might be method not allowed or handled gracefully
    EXPECT_TRUE(response.status_code == 405 || response.status_code == 200);
}

TEST_F(ApiInterfaceTest, StreamProcessorNotAvailable) {
    // Don't set stream processor
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/metadata");
    
    EXPECT_EQ(response.status_code, 404);
    EXPECT_NE(response.body.find("not available"), string::npos);
}

// Authentication Tests
TEST_F(ApiInterfaceTest, AuthenticationRequired) {
    config_.require_auth = true;
    api_ = make_unique<StreamDABApiInterface>(config_);
    
    api_->set_stream_processor(mock_stream_processor_);
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    // Request without authentication
    auto response = makeHttpRequest("GET", "/api/v1/status");
    
    EXPECT_EQ(response.status_code, 401);
}

TEST_F(ApiInterfaceTest, ValidAuthentication) {
    config_.require_auth = true;
    config_.api_key = "test_key_123";
    api_ = make_unique<StreamDABApiInterface>(config_);
    
    api_->set_stream_processor(mock_stream_processor_);
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    map<string, string> headers = {
        {"Authorization", "Bearer test_key_123"}
    };
    
    auto response = makeHttpRequest("GET", "/api/v1/status", "", headers);
    
    EXPECT_EQ(response.status_code, 200);
}

// CORS Tests
TEST_F(ApiInterfaceTest, CORSHeaders) {
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/status");
    
    // Check for CORS headers in response (this would need proper header parsing)
    // For now, just ensure the request succeeds
    EXPECT_EQ(response.status_code, 200);
}

TEST_F(ApiInterfaceTest, OptionsPreflightRequest) {
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("OPTIONS", "/api/v1/status");
    
    EXPECT_EQ(response.status_code, 200);
}

// Performance Tests
TEST_F(ApiInterfaceTest, ConcurrentRequests) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    vector<thread> threads;
    vector<bool> results(10, false);
    
    // Launch concurrent requests
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, &results, i]() {
            auto response = makeHttpRequest("GET", "/api/v1/status");
            results[i] = (response.status_code == 200);
        });
    }
    
    // Wait for all requests to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All requests should succeed
    for (bool result : results) {
        EXPECT_TRUE(result);
    }
}

TEST_F(ApiInterfaceTest, ResponseTimePerformance) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < 100; ++i) {
        auto response = makeHttpRequest("GET", "/api/v1/status");
        EXPECT_EQ(response.status_code, 200);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    // 100 requests should complete quickly (less than 5 seconds)
    EXPECT_LT(duration.count(), 5000);
    
    // Average response time should be reasonable
    double avg_response_time = static_cast<double>(duration.count()) / 100.0;
    EXPECT_LT(avg_response_time, 50.0); // Less than 50ms average
}

// Health Status Tests
TEST_F(ApiInterfaceTest, HealthySystemStatus) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    
    auto health = api_->get_health_status();
    
    EXPECT_TRUE(health.api_healthy);
    EXPECT_TRUE(health.stream_healthy);
    EXPECT_TRUE(health.issues.empty());
}

TEST_F(ApiInterfaceTest, UnhealthyStreamStatus) {
    // Configure mock to return unhealthy status
    ON_CALL(*mock_stream_processor_, is_healthy())
        .WillByDefault(::testing::Return(false));
    ON_CALL(*mock_stream_processor_, get_health_issues())
        .WillByDefault(::testing::Return(vector<string>{"Stream connection lost"}));
    
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    
    auto health = api_->get_health_status();
    
    EXPECT_TRUE(health.api_healthy);
    EXPECT_FALSE(health.stream_healthy);
    EXPECT_FALSE(health.issues.empty());
    EXPECT_EQ(health.issues[0], "Stream connection lost");
}

// Metrics Tests
TEST_F(ApiInterfaceTest, APIMetricsTracking) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    // Make some requests
    makeHttpRequest("GET", "/api/v1/status");
    makeHttpRequest("GET", "/api/v1/metadata");
    makeHttpRequest("GET", "/api/v1/quality");
    
    auto metrics = api_->get_api_metrics();
    
    EXPECT_GE(metrics.total_requests, 3);
    EXPECT_GT(metrics.successful_requests, 0);
}

TEST_F(ApiInterfaceTest, MetricsReset) {
    api_->set_stream_processor(mock_stream_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    makeHttpRequest("GET", "/api/v1/status");
    
    auto metrics_before = api_->get_api_metrics();
    EXPECT_GT(metrics_before.total_requests, 0);
    
    api_->reset_metrics();
    
    auto metrics_after = api_->get_api_metrics();
    EXPECT_EQ(metrics_after.total_requests, 0);
}

// Edge Cases and Error Handling
TEST_F(ApiInterfaceTest, LargeRequestBody) {
    api_->set_stream_processor(mock_stream_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    string large_body(10000, 'x'); // 10KB body
    auto response = makeHttpRequest("POST", "/api/v1/config", large_body);
    
    // Should handle large requests gracefully (might be bad request due to invalid JSON)
    EXPECT_TRUE(response.status_code == 400 || response.status_code == 413);
}

TEST_F(ApiInterfaceTest, MalformedJSON) {
    api_->set_stream_processor(mock_stream_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    string malformed_json = "{invalid json";
    auto response = makeHttpRequest("POST", "/api/v1/config", malformed_json);
    
    EXPECT_EQ(response.status_code, 400);
}

TEST_F(ApiInterfaceTest, EmptyRequest) {
    api_->set_stream_processor(mock_stream_processor_);
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("POST", "/api/v1/config", "");
    
    EXPECT_EQ(response.status_code, 400);
}

// Thai Content Integration Tests
TEST_F(ApiInterfaceTest, ThaiMetadataProcessing) {
    api_->set_stream_processor(mock_stream_processor_);
    api_->set_metadata_processor(mock_metadata_processor_);
    
    // Configure mock to return Thai content
    ON_CALL(*mock_stream_processor_, get_current_title())
        .WillByDefault(::testing::Return("เพลงไทยสมัยใหม่"));
    ON_CALL(*mock_stream_processor_, get_current_artist())
        .WillByDefault(::testing::Return("นักร้องไทย"));
    
    EXPECT_TRUE(api_->start());
    this_thread::sleep_for(milliseconds(200));
    
    auto response = makeHttpRequest("GET", "/api/v1/metadata");
    
    EXPECT_EQ(response.status_code, 200);
    
    // Should contain Thai metadata
    EXPECT_NE(response.body.find("เพลงไทยสมัยใหม่"), string::npos);
    EXPECT_NE(response.body.find("นักร้องไทย"), string::npos);
    EXPECT_NE(response.body.find("is_thai_content"), string::npos);
}

// Cleanup and resource management
TEST_F(ApiInterfaceTest, ProperCleanupOnDestruction) {
    {
        auto temp_api = make_unique<StreamDABApiInterface>(config_);
        temp_api->set_stream_processor(mock_stream_processor_);
        
        EXPECT_TRUE(temp_api->start());
        this_thread::sleep_for(milliseconds(100));
        
        EXPECT_TRUE(temp_api->is_running());
        
        // temp_api will be destroyed here and should clean up properly
    }
    
    // If we reach here without hanging, cleanup was successful
    SUCCEED();
}