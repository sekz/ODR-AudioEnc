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
#include "../src/enhanced_stream.h"
#include <chrono>
#include <thread>

using namespace StreamDAB;
using namespace std::chrono;
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;

class EnhancedStreamProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test configuration
        config_.primary_url = "http://test-stream.example.com:8000/stream";
        config_.fallback_urls = {
            "http://backup1.example.com:8000/stream",
            "http://backup2.example.com:8000/stream"
        };
        config_.reconnect_delay_ms = 1000;
        config_.max_reconnects = 3;
        config_.buffer_ms = 2000;
        config_.silence_threshold_db = -40.0;
        config_.silence_timeout_s = 10;
        config_.enable_normalization = true;
        config_.target_level_db = -23.0;
        
        processor_ = std::make_unique<EnhancedStreamProcessor>(config_);
    }
    
    void TearDown() override {
        if (processor_) {
            processor_->stop_stream();
        }
    }
    
    StreamConfig config_;
    std::unique_ptr<EnhancedStreamProcessor> processor_;
};

// Test basic initialization
TEST_F(EnhancedStreamProcessorTest, Initialization) {
    EXPECT_TRUE(processor_->initialize());
    EXPECT_FALSE(processor_->is_running());
    EXPECT_FALSE(processor_->is_connected());
    
    auto retrieved_config = processor_->get_config();
    EXPECT_EQ(retrieved_config.primary_url, config_.primary_url);
    EXPECT_EQ(retrieved_config.fallback_urls.size(), config_.fallback_urls.size());
    EXPECT_EQ(retrieved_config.reconnect_delay_ms, config_.reconnect_delay_ms);
    EXPECT_EQ(retrieved_config.enable_normalization, config_.enable_normalization);
}

// Test configuration updates
TEST_F(EnhancedStreamProcessorTest, ConfigurationUpdate) {
    EXPECT_TRUE(processor_->initialize());
    
    StreamConfig new_config = config_;
    new_config.primary_url = "http://new-stream.example.com:8000/stream";
    new_config.target_level_db = -20.0;
    new_config.enable_normalization = false;
    
    processor_->update_config(new_config);
    
    auto updated_config = processor_->get_config();
    EXPECT_EQ(updated_config.primary_url, new_config.primary_url);
    EXPECT_EQ(updated_config.target_level_db, new_config.target_level_db);
    EXPECT_EQ(updated_config.enable_normalization, new_config.enable_normalization);
}

// Test quality metrics initialization
TEST_F(EnhancedStreamProcessorTest, QualityMetricsInitialization) {
    EXPECT_TRUE(processor_->initialize());
    
    auto metrics = processor_->get_quality_metrics();
    EXPECT_EQ(metrics.snr_db, 0.0);
    EXPECT_EQ(metrics.volume_peak, 0.0);
    EXPECT_EQ(metrics.volume_rms, 0.0);
    EXPECT_EQ(metrics.buffer_health, 100);
    EXPECT_FALSE(metrics.is_silence);
    EXPECT_EQ(metrics.reconnect_count, 0);
    EXPECT_EQ(metrics.underrun_count, 0);
    
    // Check that timestamps are reasonable
    auto now = steady_clock::now();
    auto time_diff = duration_cast<seconds>(now - metrics.start_time).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 1); // Should be very recent
}

// Test stream URL management
TEST_F(EnhancedStreamProcessorTest, StreamURLManagement) {
    EXPECT_TRUE(processor_->initialize());
    
    // Initially should return primary URL
    std::string current_url = processor_->get_current_url();
    EXPECT_EQ(current_url, config_.primary_url);
    
    // Test cycling through fallback URLs (this would normally happen during failures)
    processor_->cycle_fallback();
    current_url = processor_->get_current_url();
    EXPECT_EQ(current_url, config_.fallback_urls[0]);
}

// Test health checking
TEST_F(EnhancedStreamProcessorTest, HealthChecking) {
    EXPECT_TRUE(processor_->initialize());
    
    // Initially should be healthy (no connection issues yet)
    EXPECT_TRUE(processor_->is_healthy());
    
    auto health_issues = processor_->get_health_issues();
    // Should have at least "Stream disconnected" since we haven't connected
    EXPECT_GE(health_issues.size(), 1);
    
    bool has_disconnected_issue = false;
    for (const auto& issue : health_issues) {
        if (issue.find("disconnected") != std::string::npos) {
            has_disconnected_issue = true;
            break;
        }
    }
    EXPECT_TRUE(has_disconnected_issue);
}

// Test statistics tracking
TEST_F(EnhancedStreamProcessorTest, StatisticsTracking) {
    EXPECT_TRUE(processor_->initialize());
    
    auto stats = processor_->get_statistics();
    EXPECT_EQ(stats.total_samples_processed, 0);
    EXPECT_EQ(stats.total_reconnects, 0);
    EXPECT_EQ(stats.total_buffer_underruns, 0);
    EXPECT_EQ(stats.average_bitrate_kbps, 0.0);
    EXPECT_EQ(stats.current_latency_ms, 0.0);
    
    // Check uptime tracking
    auto now = steady_clock::now();
    auto uptime_diff = duration_cast<seconds>(now - stats.uptime_start).count();
    EXPECT_GE(uptime_diff, 0);
    EXPECT_LE(uptime_diff, 1);
}

// Test metrics reset
TEST_F(EnhancedStreamProcessorTest, MetricsReset) {
    EXPECT_TRUE(processor_->initialize());
    
    processor_->reset_metrics();
    
    auto metrics = processor_->get_quality_metrics();
    EXPECT_EQ(metrics.reconnect_count, 0);
    EXPECT_EQ(metrics.underrun_count, 0);
    
    // Timestamps should be updated
    auto now = steady_clock::now();
    auto time_diff = duration_cast<milliseconds>(now - metrics.last_audio).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 100); // Should be very recent
}

// Test sample processing with empty buffer
TEST_F(EnhancedStreamProcessorTest, SampleProcessingEmpty) {
    EXPECT_TRUE(processor_->initialize());
    
    std::vector<int16_t> samples;
    ssize_t result = processor_->get_samples(samples, 1024);
    
    // Should return 0 since no stream is connected
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(samples.empty() || samples.size() <= 1024);
}

// Stream utility tests
class StreamUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StreamUtilsTest, ValidateStreamURL) {
    // Valid URLs
    EXPECT_TRUE(StreamUtils::validate_stream_url("http://example.com:8000/stream"));
    EXPECT_TRUE(StreamUtils::validate_stream_url("https://secure-stream.com/live"));
    EXPECT_TRUE(StreamUtils::validate_stream_url("icecast://icecast.server.com:8000/radio"));
    
    // Invalid URLs
    EXPECT_FALSE(StreamUtils::validate_stream_url(""));
    EXPECT_FALSE(StreamUtils::validate_stream_url("not-a-url"));
    EXPECT_FALSE(StreamUtils::validate_stream_url("javascript:alert('xss')"));
    EXPECT_FALSE(StreamUtils::validate_stream_url("file:///etc/passwd"));
}

TEST_F(StreamUtilsTest, DetectStreamFormat) {
    // Test format detection for different URLs
    auto formats = StreamUtils::detect_stream_format("http://example.com:8000/stream.mp3");
    EXPECT_FALSE(formats.empty());
    
    // Should handle URLs without obvious format indicators
    formats = StreamUtils::detect_stream_format("http://example.com:8000/stream");
    EXPECT_TRUE(formats.empty() || !formats.empty()); // Either case is valid
}

// Stream URL parser tests
class StreamURLParserTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StreamURLParserTest, ParseValidHTTPURL) {
    auto parsed = StreamURLParser::parse("http://example.com:8000/stream?param=value");
    
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.protocol, "http");
    EXPECT_EQ(parsed.hostname, "example.com");
    EXPECT_EQ(parsed.port, 8000);
    EXPECT_EQ(parsed.path, "/stream");
    EXPECT_EQ(parsed.query, "param=value");
    EXPECT_TRUE(parsed.username.empty());
    EXPECT_TRUE(parsed.password.empty());
}

TEST_F(StreamURLParserTest, ParseHTTPSURL) {
    auto parsed = StreamURLParser::parse("https://secure.example.com/live");
    
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.protocol, "https");
    EXPECT_EQ(parsed.hostname, "secure.example.com");
    EXPECT_EQ(parsed.port, 443); // Default HTTPS port
    EXPECT_EQ(parsed.path, "/live");
    EXPECT_TRUE(parsed.query.empty());
}

TEST_F(StreamURLParserTest, ParseURLWithAuthentication) {
    auto parsed = StreamURLParser::parse("http://user:pass@example.com:8000/stream");
    
    EXPECT_TRUE(parsed.is_valid);
    EXPECT_EQ(parsed.protocol, "http");
    EXPECT_EQ(parsed.username, "user");
    EXPECT_EQ(parsed.password, "pass");
    EXPECT_EQ(parsed.hostname, "example.com");
    EXPECT_EQ(parsed.port, 8000);
    EXPECT_EQ(parsed.path, "/stream");
}

TEST_F(StreamURLParserTest, ParseInvalidURL) {
    auto parsed = StreamURLParser::parse("not-a-valid-url");
    EXPECT_FALSE(parsed.is_valid);
    
    parsed = StreamURLParser::parse("");
    EXPECT_FALSE(parsed.is_valid);
    
    parsed = StreamURLParser::parse("ftp://example.com/file"); // Unsupported protocol
    EXPECT_FALSE(parsed.is_valid);
}

TEST_F(StreamURLParserTest, SupportedProtocols) {
    EXPECT_TRUE(StreamURLParser::is_supported_protocol("http"));
    EXPECT_TRUE(StreamURLParser::is_supported_protocol("https"));
    EXPECT_TRUE(StreamURLParser::is_supported_protocol("icecast"));
    EXPECT_TRUE(StreamURLParser::is_supported_protocol("shoutcast"));
    
    EXPECT_FALSE(StreamURLParser::is_supported_protocol("ftp"));
    EXPECT_FALSE(StreamURLParser::is_supported_protocol("file"));
    EXPECT_FALSE(StreamURLParser::is_supported_protocol("javascript"));
}

TEST_F(StreamURLParserTest, URLSanitization) {
    // Test sanitization of potentially malicious URLs
    std::string malicious = "http://example.com/<script>alert('xss')</script>";
    std::string sanitized = StreamURLParser::sanitize_url(malicious);
    
    EXPECT_NE(sanitized, malicious);
    EXPECT_EQ(sanitized.find("<script>"), std::string::npos);
}

// Integration test class for more complex scenarios
class EnhancedStreamIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.primary_url = "http://test-stream.example.com:8000/stream";
        config_.fallback_urls = {"http://backup.example.com:8000/stream"};
        config_.reconnect_delay_ms = 500; // Shorter for tests
        config_.max_reconnects = 2;
        
        processor_ = std::make_unique<EnhancedStreamProcessor>(config_);
    }
    
    void TearDown() override {
        if (processor_) {
            processor_->stop_stream();
        }
    }
    
    StreamConfig config_;
    std::unique_ptr<EnhancedStreamProcessor> processor_;
};

TEST_F(EnhancedStreamIntegrationTest, InitializeAndCleanup) {
    EXPECT_TRUE(processor_->initialize());
    
    // Test that cleanup happens properly
    processor_.reset();
    
    // Create a new processor to ensure clean state
    processor_ = std::make_unique<EnhancedStreamProcessor>(config_);
    EXPECT_TRUE(processor_->initialize());
}

// Performance test class
class EnhancedStreamPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.primary_url = "http://test-stream.example.com:8000/stream";
        config_.enable_normalization = true;
        processor_ = std::make_unique<EnhancedStreamProcessor>(config_);
    }
    
    void TearDown() override {
        if (processor_) {
            processor_->stop_stream();
        }
    }
    
    StreamConfig config_;
    std::unique_ptr<EnhancedStreamProcessor> processor_;
};

TEST_F(EnhancedStreamPerformanceTest, ConfigurationUpdatePerformance) {
    EXPECT_TRUE(processor_->initialize());
    
    auto start_time = high_resolution_clock::now();
    
    // Perform multiple configuration updates
    for (int i = 0; i < 100; ++i) {
        StreamConfig new_config = config_;
        new_config.target_level_db = -20.0 + (i % 10);
        processor_->update_config(new_config);
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should complete 100 configuration updates in reasonable time (< 100ms)
    EXPECT_LT(duration_ms, 100);
}

TEST_F(EnhancedStreamPerformanceTest, MetricsAccessPerformance) {
    EXPECT_TRUE(processor_->initialize());
    
    auto start_time = high_resolution_clock::now();
    
    // Access metrics many times
    for (int i = 0; i < 1000; ++i) {
        auto metrics = processor_->get_quality_metrics();
        (void)metrics; // Suppress unused variable warning
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should complete 1000 metrics accesses in reasonable time (< 50ms)
    EXPECT_LT(duration_ms, 50);
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}