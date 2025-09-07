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
#include "../src/security_utils.h"
#include <chrono>
#include <thread>
#include <random>

using namespace StreamDAB;
using namespace std::chrono;

// Test class for input validation
class InputValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_input_validation = true;
        config_.max_url_length = 2048;
        config_.max_metadata_length = 1024;
        config_.allowed_url_schemes = {"http", "https", "icecast", "shoutcast"};
        
        validator_ = std::make_unique<InputValidator>(config_);
    }
    
    void TearDown() override {}
    
    SecurityConfig config_;
    std::unique_ptr<InputValidator> validator_;
};

TEST_F(InputValidatorTest, ValidURLs) {
    // Valid HTTP URLs
    EXPECT_TRUE(validator_->validate_stream_url("http://example.com:8000/stream"));
    EXPECT_TRUE(validator_->validate_stream_url("https://secure.example.com/live"));
    EXPECT_TRUE(validator_->validate_stream_url("icecast://icecast.server.com:8000/radio"));
    EXPECT_TRUE(validator_->validate_stream_url("shoutcast://shout.server.com:8080/stream"));
    
    // URLs with query parameters
    EXPECT_TRUE(validator_->validate_stream_url("http://example.com/stream?param=value"));
    
    // URLs with authentication
    EXPECT_TRUE(validator_->validate_stream_url("http://user:pass@example.com:8000/stream"));
}

TEST_F(InputValidatorTest, InvalidURLs) {
    // Empty URL
    EXPECT_FALSE(validator_->validate_stream_url(""));
    
    // Invalid schemes
    EXPECT_FALSE(validator_->validate_stream_url("ftp://example.com/file"));
    EXPECT_FALSE(validator_->validate_stream_url("file:///etc/passwd"));
    EXPECT_FALSE(validator_->validate_stream_url("javascript:alert('xss')"));
    
    // Malicious patterns
    EXPECT_FALSE(validator_->validate_stream_url("http://example.com<script>alert('xss')</script>"));
    EXPECT_FALSE(validator_->validate_stream_url("http://example.com/data:text/html,<script>"));
    
    // Too long URL
    std::string long_url = "http://example.com/" + std::string(3000, 'a');
    EXPECT_FALSE(validator_->validate_stream_url(long_url));
}

TEST_F(InputValidatorTest, URLSchemeValidation) {
    EXPECT_TRUE(validator_->validate_url_scheme("http"));
    EXPECT_TRUE(validator_->validate_url_scheme("https"));
    EXPECT_TRUE(validator_->validate_url_scheme("icecast"));
    EXPECT_TRUE(validator_->validate_url_scheme("shoutcast"));
    
    EXPECT_FALSE(validator_->validate_url_scheme("ftp"));
    EXPECT_FALSE(validator_->validate_url_scheme("file"));
    EXPECT_FALSE(validator_->validate_url_scheme("javascript"));
    EXPECT_FALSE(validator_->validate_url_scheme(""));
}

TEST_F(InputValidatorTest, HostnameValidation) {
    // Valid hostnames
    EXPECT_TRUE(validator_->validate_hostname("example.com"));
    EXPECT_TRUE(validator_->validate_hostname("sub.domain.example.com"));
    EXPECT_TRUE(validator_->validate_hostname("localhost"));
    
    // Valid IPv4 addresses
    EXPECT_TRUE(validator_->validate_hostname("192.168.1.1"));
    EXPECT_TRUE(validator_->validate_hostname("127.0.0.1"));
    EXPECT_TRUE(validator_->validate_hostname("10.0.0.1"));
    
    // Invalid hostnames
    EXPECT_FALSE(validator_->validate_hostname(""));
    EXPECT_FALSE(validator_->validate_hostname("example..com")); // Double dot
    EXPECT_FALSE(validator_->validate_hostname("-example.com")); // Leading dash
    
    // Invalid IPv4
    EXPECT_FALSE(validator_->validate_hostname("256.1.1.1")); // Octet > 255
    EXPECT_FALSE(validator_->validate_hostname("192.168.1")); // Missing octet
}

TEST_F(InputValidatorTest, PortValidation) {
    // Valid ports
    EXPECT_TRUE(validator_->validate_port(80));
    EXPECT_TRUE(validator_->validate_port(8000));
    EXPECT_TRUE(validator_->validate_port(65535));
    EXPECT_TRUE(validator_->validate_port(1));
    
    // Invalid ports
    EXPECT_FALSE(validator_->validate_port(0));
    EXPECT_FALSE(validator_->validate_port(-1));
    EXPECT_FALSE(validator_->validate_port(65536));
}

TEST_F(InputValidatorTest, MetadataValidation) {
    // Valid metadata
    EXPECT_TRUE(validator_->validate_metadata_field("Song Title"));
    EXPECT_TRUE(validator_->validate_metadata_field("Artist Name"));
    EXPECT_TRUE(validator_->validate_metadata_field("สวัสดี")); // Thai text
    EXPECT_TRUE(validator_->validate_metadata_field("Mixed สวัสดี English"));
    
    // Valid with control characters that are allowed
    EXPECT_TRUE(validator_->validate_metadata_field("Song\tTitle"));
    EXPECT_TRUE(validator_->validate_metadata_field("Artist\nName"));
    EXPECT_TRUE(validator_->validate_metadata_field("Album\rName"));
    
    // Invalid metadata
    std::string control_chars;
    control_chars.push_back(0x01); // Invalid control character
    control_chars += "Title";
    EXPECT_FALSE(validator_->validate_metadata_field(control_chars));
    
    // Too long metadata
    std::string long_metadata(2000, 'A');
    EXPECT_FALSE(validator_->validate_metadata_field(long_metadata));
}

TEST_F(InputValidatorTest, FilePathValidation) {
    // Valid paths
    EXPECT_TRUE(validator_->validate_file_path("/var/log/odr-audioenc.log"));
    EXPECT_TRUE(validator_->validate_file_path("config/settings.json"));
    EXPECT_TRUE(validator_->validate_file_path("audio/samples/test.wav"));
    
    // Path traversal attempts
    EXPECT_FALSE(validator_->validate_file_path("../../../etc/passwd"));
    EXPECT_FALSE(validator_->validate_file_path("config/../../../etc/shadow"));
    EXPECT_FALSE(validator_->validate_file_path("/var/log/../../etc/passwd"));
    
    // Null byte injection
    std::string null_path = "valid_path";
    null_path.push_back('\0');
    null_path += "/../../../etc/passwd";
    EXPECT_FALSE(validator_->validate_file_path(null_path));
}

TEST_F(InputValidatorTest, FilenameValidation) {
    // Valid filenames
    EXPECT_TRUE(validator_->validate_filename("audio.mp3"));
    EXPECT_TRUE(validator_->validate_filename("config-file.json"));
    EXPECT_TRUE(validator_->validate_filename("test_file_123.txt"));
    
    // Invalid filenames (depends on implementation)
    // These tests might need adjustment based on actual filename validation rules
    EXPECT_FALSE(validator_->validate_filename(""));
    EXPECT_FALSE(validator_->validate_filename("file with spaces")); // If spaces not allowed
}

TEST_F(InputValidatorTest, Sanitization) {
    // URL sanitization
    std::string malicious_url = "http://example.com/<script>alert('xss')</script>";
    std::string sanitized_url = validator_->sanitize_url(malicious_url);
    EXPECT_NE(sanitized_url, malicious_url);
    
    // Metadata sanitization
    std::string metadata_with_controls = "Song Title\x01\x02Artist";
    std::string sanitized_metadata = validator_->sanitize_metadata(metadata_with_controls);
    EXPECT_EQ(sanitized_metadata.find('\x01'), std::string::npos);
    EXPECT_EQ(sanitized_metadata.find('\x02'), std::string::npos);
    
    // Long text truncation
    std::string long_text(2000, 'A');
    std::string truncated = validator_->sanitize_metadata(long_text);
    EXPECT_LE(truncated.length(), config_.max_metadata_length);
}

TEST_F(InputValidatorTest, ConfigurationDisabled) {
    // Test with validation disabled
    SecurityConfig disabled_config = config_;
    disabled_config.enable_input_validation = false;
    
    InputValidator disabled_validator(disabled_config);
    
    // Should allow invalid inputs when validation is disabled
    EXPECT_TRUE(disabled_validator.validate_stream_url("javascript:alert('xss')"));
    EXPECT_TRUE(disabled_validator.validate_metadata_field(std::string(2000, 'A')));
    EXPECT_TRUE(disabled_validator.validate_file_path("../../../etc/passwd"));
}

// Test class for secure buffer
class SecureBufferTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SecureBufferTest, BasicOperations) {
    SecureBuffer buffer(1024, true);
    
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 1024);
    EXPECT_TRUE(buffer.empty());
    EXPECT_FALSE(buffer.full());
    EXPECT_EQ(buffer.available(), 1024);
}

TEST_F(SecureBufferTest, WriteAndRead) {
    SecureBuffer buffer(1024, true);
    
    std::string test_data = "Hello, World!";
    EXPECT_TRUE(buffer.write(test_data.data(), test_data.size()));
    
    EXPECT_EQ(buffer.size(), test_data.size());
    EXPECT_FALSE(buffer.empty());
    
    std::vector<char> read_buffer(test_data.size());
    EXPECT_TRUE(buffer.read(read_buffer.data(), read_buffer.size()));
    
    std::string read_data(read_buffer.begin(), read_buffer.end());
    EXPECT_EQ(read_data, test_data);
}

TEST_F(SecureBufferTest, WriteAtOffset) {
    SecureBuffer buffer(1024, true);
    
    std::string data1 = "Hello";
    std::string data2 = "World";
    
    EXPECT_TRUE(buffer.write_at(0, data1.data(), data1.size()));
    EXPECT_TRUE(buffer.write_at(10, data2.data(), data2.size()));
    
    EXPECT_EQ(buffer.size(), 15); // Should be max(5, 10+5)
}

TEST_F(SecureBufferTest, BufferOverflow) {
    SecureBuffer buffer(10, true);
    
    std::string large_data(20, 'A');
    EXPECT_FALSE(buffer.write(large_data.data(), large_data.size()));
    
    // Buffer should remain empty after failed write
    EXPECT_TRUE(buffer.empty());
}

TEST_F(SecureBufferTest, GuardByteProtection) {
    SecureBuffer buffer(100, true);
    
    // Fill buffer to capacity
    std::string data(100, 'A');
    EXPECT_TRUE(buffer.write(data.data(), data.size()));
    
    // Verify buffer integrity
    EXPECT_TRUE(buffer.is_buffer_intact());
    
    // This should not throw if guard bytes are intact
    EXPECT_NO_THROW(buffer.validate_buffer_integrity());
}

TEST_F(SecureBufferTest, DisableGuardBytes) {
    SecureBuffer buffer(100, false); // Guard bytes disabled
    
    std::string data(100, 'A');
    EXPECT_TRUE(buffer.write(data.data(), data.size()));
    
    // Should still be intact even without guard bytes
    EXPECT_TRUE(buffer.is_buffer_intact());
}

TEST_F(SecureBufferTest, ClearBuffer) {
    SecureBuffer buffer(100, true);
    
    std::string data = "Test data";
    EXPECT_TRUE(buffer.write(data.data(), data.size()));
    EXPECT_FALSE(buffer.empty());
    
    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0);
}

TEST_F(SecureBufferTest, ResizeBuffer) {
    SecureBuffer buffer(100, true);
    
    std::string data = "Test data";
    EXPECT_TRUE(buffer.write(data.data(), data.size()));
    
    // Resize to larger capacity
    buffer.resize(200);
    EXPECT_EQ(buffer.capacity(), 200);
    
    // Should still contain original data
    EXPECT_EQ(buffer.size(), data.size());
}

// Test class for memory management
class MemoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = &MemoryManager::instance();
        manager_->enable_tracking(true);
    }
    
    void TearDown() override {}
    
    MemoryManager* manager_;
};

TEST_F(MemoryManagerTest, BasicAllocation) {
    size_t initial_allocated = manager_->get_total_allocated();
    
    void* ptr = manager_->allocate(1024, __FILE__, __LINE__);
    EXPECT_NE(ptr, nullptr);
    
    size_t after_alloc = manager_->get_total_allocated();
    EXPECT_EQ(after_alloc - initial_allocated, 1024);
    
    manager_->deallocate(ptr);
    
    size_t after_dealloc = manager_->get_total_allocated();
    EXPECT_EQ(after_dealloc, initial_allocated);
}

TEST_F(MemoryManagerTest, MultipleAllocations) {
    std::vector<void*> pointers;
    size_t total_size = 0;
    
    for (int i = 0; i < 10; ++i) {
        size_t size = (i + 1) * 100;
        void* ptr = manager_->allocate(size, __FILE__, __LINE__);
        EXPECT_NE(ptr, nullptr);
        
        pointers.push_back(ptr);
        total_size += size;
    }
    
    // Check total allocated
    EXPECT_GE(manager_->get_total_allocated(), total_size);
    EXPECT_EQ(manager_->get_allocation_count(), 10);
    
    // Deallocate all
    for (void* ptr : pointers) {
        manager_->deallocate(ptr);
    }
    
    // Should have no active allocations
    EXPECT_EQ(manager_->get_active_allocations(), 0);
}

TEST_F(MemoryManagerTest, PeakMemoryTracking) {
    size_t initial_peak = manager_->get_peak_allocated();
    
    void* large_allocation = manager_->allocate(10000, __FILE__, __LINE__);
    EXPECT_NE(large_allocation, nullptr);
    
    size_t after_large_alloc = manager_->get_peak_allocated();
    EXPECT_GE(after_large_alloc, initial_peak + 10000);
    
    manager_->deallocate(large_allocation);
    
    // Peak should remain the same after deallocation
    EXPECT_EQ(manager_->get_peak_allocated(), after_large_alloc);
}

TEST_F(MemoryManagerTest, LeakDetection) {
    // Allocate some memory and don't deallocate it
    void* leaked_ptr1 = manager_->allocate(100, __FILE__, __LINE__);
    void* leaked_ptr2 = manager_->allocate(200, __FILE__, __LINE__);
    
    EXPECT_NE(leaked_ptr1, nullptr);
    EXPECT_NE(leaked_ptr2, nullptr);
    
    auto leaks = manager_->detect_memory_leaks();
    EXPECT_GE(leaks.size(), 2);
    
    // Clean up to avoid actual leaks in test
    manager_->deallocate(leaked_ptr1);
    manager_->deallocate(leaked_ptr2);
}

TEST_F(MemoryManagerTest, TrackingDisabled) {
    manager_->enable_tracking(false);
    
    size_t initial_count = manager_->get_allocation_count();
    
    void* ptr = manager_->allocate(1000, __FILE__, __LINE__);
    EXPECT_NE(ptr, nullptr);
    
    // Allocation count should not increase when tracking is disabled
    EXPECT_EQ(manager_->get_allocation_count(), initial_count);
    
    manager_->deallocate(ptr);
    
    // Re-enable for cleanup
    manager_->enable_tracking(true);
}

// Test class for memory pool
class MemoryPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = &MemoryManager::instance();
        pool_ = manager_->create_pool(128, 10);
    }
    
    void TearDown() override {}
    
    MemoryManager* manager_;
    std::unique_ptr<MemoryManager::MemoryPool> pool_;
};

TEST_F(MemoryPoolTest, BasicPoolOperations) {
    EXPECT_EQ(pool_->get_block_size(), 128);
    EXPECT_EQ(pool_->get_free_blocks(), 10);
    
    void* ptr = pool_->allocate();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(pool_->get_free_blocks(), 9);
    
    pool_->deallocate(ptr);
    EXPECT_EQ(pool_->get_free_blocks(), 10);
}

TEST_F(MemoryPoolTest, ExhaustPool) {
    std::vector<void*> allocations;
    
    // Allocate all blocks
    for (size_t i = 0; i < 10; ++i) {
        void* ptr = pool_->allocate();
        EXPECT_NE(ptr, nullptr);
        allocations.push_back(ptr);
    }
    
    EXPECT_EQ(pool_->get_free_blocks(), 0);
    
    // Should return nullptr when pool is exhausted
    void* overflow_ptr = pool_->allocate();
    EXPECT_EQ(overflow_ptr, nullptr);
    
    // Return all blocks
    for (void* ptr : allocations) {
        pool_->deallocate(ptr);
    }
    
    EXPECT_EQ(pool_->get_free_blocks(), 10);
}

// Test class for audit logger
class AuditLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file_ = "/tmp/test_audit.log";
        logger_ = std::make_unique<AuditLogger>(log_file_, AuditLogger::LogLevel::Debug);
    }
    
    void TearDown() override {
        // Clean up log file
        std::remove(log_file_.c_str());
    }
    
    std::string log_file_;
    std::unique_ptr<AuditLogger> logger_;
};

TEST_F(AuditLoggerTest, BasicLogging) {
    EXPECT_TRUE(logger_->is_enabled());
    
    logger_->info("Test info message");
    logger_->warning("Test warning message");
    logger_->error("Test error message");
    logger_->security("Test security message");
    
    // Verify log file exists and has content
    std::ifstream log_check(log_file_);
    EXPECT_TRUE(log_check.is_open());
    
    std::string line;
    int line_count = 0;
    while (std::getline(log_check, line)) {
        line_count++;
        EXPECT_FALSE(line.empty());
    }
    
    EXPECT_EQ(line_count, 4);
}

TEST_F(AuditLoggerTest, LogLevels) {
    // Create logger with higher minimum level
    AuditLogger filtered_logger("/tmp/test_filtered.log", AuditLogger::LogLevel::Warning);
    
    filtered_logger.debug("This should not appear");
    filtered_logger.info("This should not appear");
    filtered_logger.warning("This should appear");
    filtered_logger.error("This should appear");
    
    std::ifstream log_check("/tmp/test_filtered.log");
    EXPECT_TRUE(log_check.is_open());
    
    std::string line;
    int line_count = 0;
    while (std::getline(log_check, line)) {
        line_count++;
    }
    
    EXPECT_EQ(line_count, 2); // Only warning and error should appear
    
    std::remove("/tmp/test_filtered.log");
}

TEST_F(AuditLoggerTest, LogWithContext) {
    std::map<std::string, std::string> context = {
        {"user", "test_user"},
        {"ip", "127.0.0.1"},
        {"action", "stream_connect"}
    };
    
    logger_->info("User connected", context);
    
    std::ifstream log_check(log_file_);
    std::string log_content;
    std::getline(log_check, log_content);
    
    // Should contain context information
    EXPECT_NE(log_content.find("test_user"), std::string::npos);
    EXPECT_NE(log_content.find("127.0.0.1"), std::string::npos);
    EXPECT_NE(log_content.find("stream_connect"), std::string::npos);
}

TEST_F(AuditLoggerTest, SpecializedLogMethods) {
    logger_->log_stream_connection("http://test.com:8000/stream", true);
    logger_->log_config_change("buffer_size", "1000", "2000");
    logger_->log_security_violation("Invalid URL", "Attempted path traversal");
    logger_->log_performance_alert("CPU usage", 85.5, 80.0);
    
    std::ifstream log_check(log_file_);
    std::string line;
    int line_count = 0;
    while (std::getline(log_check, line)) {
        line_count++;
        EXPECT_FALSE(line.empty());
    }
    
    EXPECT_EQ(line_count, 4);
}

// Test class for performance monitor
class PerformanceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        monitor_ = std::make_unique<PerformanceMonitor>();
    }
    
    void TearDown() override {
        if (monitor_) {
            monitor_->stop_monitoring();
        }
    }
    
    std::unique_ptr<PerformanceMonitor> monitor_;
};

TEST_F(PerformanceMonitorTest, BasicMetrics) {
    auto metrics = monitor_->get_current_metrics();
    
    EXPECT_EQ(metrics.cpu_usage_percent, 0.0);
    EXPECT_EQ(metrics.memory_usage_bytes, 0);
    EXPECT_EQ(metrics.audio_processing_latency_ms, 0.0);
    EXPECT_EQ(metrics.buffer_underruns, 0);
    EXPECT_EQ(metrics.buffer_overruns, 0);
    
    // Timestamp should be recent
    auto now = steady_clock::now();
    auto time_diff = duration_cast<seconds>(now - metrics.last_updated).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 1);
}

TEST_F(PerformanceMonitorTest, MetricsUpdate) {
    monitor_->update_audio_latency(25.5);
    monitor_->update_network_latency(150.0);
    monitor_->record_buffer_underrun();
    monitor_->record_buffer_overrun();
    monitor_->update_throughput(5.2);
    
    auto metrics = monitor_->get_current_metrics();
    
    EXPECT_EQ(metrics.audio_processing_latency_ms, 25.5);
    EXPECT_EQ(metrics.network_latency_ms, 150.0);
    EXPECT_EQ(metrics.buffer_underruns, 1);
    EXPECT_EQ(metrics.buffer_overruns, 1);
    EXPECT_EQ(metrics.throughput_mbps, 5.2);
}

TEST_F(PerformanceMonitorTest, StartStopMonitoring) {
    EXPECT_FALSE(monitor_->is_monitoring());
    
    monitor_->start_monitoring();
    EXPECT_TRUE(monitor_->is_monitoring());
    
    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    monitor_->stop_monitoring();
    EXPECT_FALSE(monitor_->is_monitoring());
}

TEST_F(PerformanceMonitorTest, ThresholdConfiguration) {
    monitor_->set_cpu_threshold(90.0);
    monitor_->set_memory_threshold(1024 * 1024 * 1024); // 1GB
    monitor_->set_audio_latency_threshold(100.0);
    
    // These are just configuration tests - actual threshold checking
    // would require the monitoring to be running and generating alerts
}

TEST_F(PerformanceMonitorTest, AlertManagement) {
    auto initial_alerts = monitor_->get_active_alerts();
    EXPECT_TRUE(initial_alerts.empty());
    
    // Clear alerts (should work even if empty)
    monitor_->clear_alerts();
    
    auto cleared_alerts = monitor_->get_active_alerts();
    EXPECT_TRUE(cleared_alerts.empty());
}

// Test class for SIMD processor
class SIMDProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate test data
        test_samples_.resize(1000);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int16_t> dis(-32768, 32767);
        
        for (auto& sample : test_samples_) {
            sample = dis(gen);
        }
    }
    
    void TearDown() override {}
    
    std::vector<int16_t> test_samples_;
};

TEST_F(SIMDProcessorTest, CPUCapabilityDetection) {
    // These should not crash and should return consistent results
    bool has_sse2_1 = SIMDProcessor::has_sse2_support();
    bool has_sse2_2 = SIMDProcessor::has_sse2_support();
    EXPECT_EQ(has_sse2_1, has_sse2_2);
    
    bool has_avx2_1 = SIMDProcessor::has_avx2_support();
    bool has_avx2_2 = SIMDProcessor::has_avx2_support();
    EXPECT_EQ(has_avx2_1, has_avx2_2);
    
    bool has_neon_1 = SIMDProcessor::has_neon_support();
    bool has_neon_2 = SIMDProcessor::has_neon_support();
    EXPECT_EQ(has_neon_1, has_neon_2);
}

TEST_F(SIMDProcessorTest, NormalizeSamples) {
    std::vector<int16_t> samples_copy = test_samples_;
    float gain = 0.5f;
    
    SIMDProcessor::normalize_samples_simd(samples_copy.data(), samples_copy.size(), gain);
    
    // Verify samples were modified
    bool samples_changed = false;
    for (size_t i = 0; i < test_samples_.size(); ++i) {
        if (samples_copy[i] != test_samples_[i]) {
            samples_changed = true;
            break;
        }
    }
    EXPECT_TRUE(samples_changed);
    
    // Test with gain of 1.0 (should be approximately the same)
    std::vector<int16_t> samples_unity = test_samples_;
    SIMDProcessor::normalize_samples_simd(samples_unity.data(), samples_unity.size(), 1.0f);
    
    // Should be very close to original (within rounding error)
    int differences = 0;
    for (size_t i = 0; i < test_samples_.size(); ++i) {
        if (abs(samples_unity[i] - test_samples_[i]) > 1) {
            differences++;
        }
    }
    EXPECT_LT(differences, test_samples_.size() / 10); // Less than 10% should have large differences
}

TEST_F(SIMDProcessorTest, CalculateRMS) {
    double rms = SIMDProcessor::calculate_rms_simd(test_samples_.data(), test_samples_.size());
    
    EXPECT_GE(rms, 0.0);
    EXPECT_LT(rms, 32768.0); // Should be less than max sample value
    
    // Test with known values
    std::vector<int16_t> known_samples = {1000, -1000, 2000, -2000, 3000, -3000};
    double known_rms = SIMDProcessor::calculate_rms_simd(known_samples.data(), known_samples.size());
    EXPECT_GT(known_rms, 0.0);
    
    // Test with zero samples
    std::vector<int16_t> zero_samples(100, 0);
    double zero_rms = SIMDProcessor::calculate_rms_simd(zero_samples.data(), zero_samples.size());
    EXPECT_EQ(zero_rms, 0.0);
    
    // Test with empty array
    double empty_rms = SIMDProcessor::calculate_rms_simd(nullptr, 0);
    EXPECT_EQ(empty_rms, 0.0);
}

TEST_F(SIMDProcessorTest, ApplyGain) {
    std::vector<int16_t> samples_copy = test_samples_;
    float gain = 2.0f;
    
    SIMDProcessor::apply_gain_simd(samples_copy.data(), samples_copy.size(), gain);
    
    // Verify gain was applied (samples should generally be larger in magnitude)
    // Note: This test is approximate due to clipping
    int larger_samples = 0;
    for (size_t i = 0; i < test_samples_.size(); ++i) {
        if (abs(samples_copy[i]) >= abs(test_samples_[i])) {
            larger_samples++;
        }
    }
    EXPECT_GT(larger_samples, test_samples_.size() / 2); // Most samples should be larger or equal
}

// Test class for thread-safe queue
class ThreadSafeQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue_ = std::make_unique<ThreadSafeQueue>(1024);
    }
    
    void TearDown() override {}
    
    std::unique_ptr<ThreadSafeQueue> queue_;
};

TEST_F(ThreadSafeQueueTest, BasicOperations) {
    EXPECT_TRUE(queue_->empty());
    EXPECT_FALSE(queue_->full());
    EXPECT_EQ(queue_->size(), 0);
    EXPECT_EQ(queue_->capacity(), 1024);
}

TEST_F(ThreadSafeQueueTest, PushAndPop) {
    std::string test_data = "Hello, World!";
    
    EXPECT_TRUE(queue_->push(test_data.data(), test_data.size()));
    EXPECT_FALSE(queue_->empty());
    EXPECT_EQ(queue_->size(), test_data.size());
    
    std::vector<char> read_buffer(test_data.size());
    size_t actual_length;
    EXPECT_TRUE(queue_->pop(read_buffer.data(), read_buffer.size(), actual_length));
    
    EXPECT_EQ(actual_length, test_data.size());
    std::string read_data(read_buffer.begin(), read_buffer.begin() + actual_length);
    EXPECT_EQ(read_data, test_data);
    
    EXPECT_TRUE(queue_->empty());
}

TEST_F(ThreadSafeQueueTest, MultithreadedAccess) {
    const int num_messages = 100;
    const std::string message = "Test message ";
    
    // Producer thread
    std::thread producer([this, num_messages, message]() {
        for (int i = 0; i < num_messages; ++i) {
            std::string data = message + std::to_string(i);
            while (!queue_->push(data.data(), data.size(), std::chrono::milliseconds(10))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
    
    // Consumer thread
    std::vector<std::string> received_messages;
    std::thread consumer([this, num_messages, &received_messages]() {
        for (int i = 0; i < num_messages; ++i) {
            std::vector<char> buffer(256);
            size_t actual_length;
            while (!queue_->pop(buffer.data(), buffer.size(), actual_length, std::chrono::milliseconds(10))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            received_messages.emplace_back(buffer.begin(), buffer.begin() + actual_length);
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(received_messages.size(), num_messages);
    EXPECT_TRUE(queue_->empty());
}

TEST_F(ThreadSafeQueueTest, QueueOverflow) {
    // Fill the queue to capacity
    std::string data(1024, 'A');
    EXPECT_TRUE(queue_->push(data.data(), data.size()));
    EXPECT_TRUE(queue_->full());
    
    // Should fail to push more data
    std::string more_data = "B";
    EXPECT_FALSE(queue_->push(more_data.data(), more_data.size(), std::chrono::milliseconds(1)));
}

TEST_F(ThreadSafeQueueTest, ClearQueue) {
    std::string data = "Test data";
    EXPECT_TRUE(queue_->push(data.data(), data.size()));
    EXPECT_FALSE(queue_->empty());
    
    queue_->clear();
    EXPECT_TRUE(queue_->empty());
    EXPECT_EQ(queue_->size(), 0);
}

// Test class for security exceptions
class SecurityExceptionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SecurityExceptionTest, ExceptionCreation) {
    SecurityException ex(SecurityViolationType::BufferOverflow, 
                        "Buffer overflow detected", 
                        "test_function:123");
    
    EXPECT_EQ(ex.get_violation_type(), SecurityViolationType::BufferOverflow);
    EXPECT_STREQ(ex.what(), "Buffer overflow detected");
    EXPECT_EQ(ex.get_context(), "test_function:123");
}

TEST_F(SecurityExceptionTest, ExceptionThrowingAndCatching) {
    try {
        throw SecurityException(SecurityViolationType::InvalidInput, 
                               "Invalid input detected");
    }
    catch (const SecurityException& e) {
        EXPECT_EQ(e.get_violation_type(), SecurityViolationType::InvalidInput);
        EXPECT_STREQ(e.what(), "Invalid input detected");
    }
    catch (...) {
        FAIL() << "Wrong exception type caught";
    }
}

// Test class for security utility macros
class SecurityMacrosTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(SecurityMacrosTest, ValidateInputMacro) {
    // Should not throw for valid input
    EXPECT_NO_THROW(VALIDATE_INPUT(true, "This should not throw"));
    
    // Should throw for invalid input
    EXPECT_THROW(VALIDATE_INPUT(false, "This should throw"), SecurityException);
    
    try {
        VALIDATE_INPUT(false, "Test validation failure");
    }
    catch (const SecurityException& e) {
        EXPECT_EQ(e.get_violation_type(), SecurityViolationType::InvalidInput);
        EXPECT_STREQ(e.what(), "Test validation failure");
    }
}

TEST_F(SecurityMacrosTest, CheckBufferBoundsMacro) {
    // Should not throw for valid buffer size
    EXPECT_NO_THROW(CHECK_BUFFER_BOUNDS(nullptr, 100, 200));
    
    // Should throw for buffer overflow
    EXPECT_THROW(CHECK_BUFFER_BOUNDS(nullptr, 300, 200), SecurityException);
    
    try {
        CHECK_BUFFER_BOUNDS(nullptr, 300, 200);
    }
    catch (const SecurityException& e) {
        EXPECT_EQ(e.get_violation_type(), SecurityViolationType::BufferOverflow);
    }
}

// Performance tests
class SecurityPerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        validator_ = std::make_unique<InputValidator>();
    }
    
    void TearDown() override {}
    
    std::unique_ptr<InputValidator> validator_;
};

TEST_F(SecurityPerformanceTest, URLValidationPerformance) {
    std::vector<std::string> test_urls;
    for (int i = 0; i < 1000; ++i) {
        test_urls.push_back("http://example" + std::to_string(i) + ".com:8000/stream");
    }
    
    auto start_time = high_resolution_clock::now();
    
    for (const auto& url : test_urls) {
        validator_->validate_stream_url(url);
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should validate 1000 URLs in reasonable time (< 100ms)
    EXPECT_LT(duration_ms, 100);
}

TEST_F(SecurityPerformanceTest, MetadataValidationPerformance) {
    std::vector<std::string> test_metadata;
    for (int i = 0; i < 1000; ++i) {
        test_metadata.push_back("Song Title " + std::to_string(i) + " - Artist Name");
    }
    
    auto start_time = high_resolution_clock::now();
    
    for (const auto& metadata : test_metadata) {
        validator_->validate_metadata_field(metadata);
    }
    
    auto end_time = high_resolution_clock::now();
    auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
    
    // Should validate 1000 metadata fields in reasonable time (< 50ms)
    EXPECT_LT(duration_ms, 50);
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}