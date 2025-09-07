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
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <map>
#include <functional>
#include <cstdint>

namespace StreamDAB {

/*! \file security_utils.h
 *  \brief Security and performance enhancements for ODR-AudioEnc
 *         including input validation, buffer protection, memory safety,
 *         and performance optimizations.
 */

// Security configuration
struct SecurityConfig {
    bool enable_input_validation = true;
    bool enable_buffer_overflow_protection = true;
    bool enable_memory_leak_detection = true;
    bool enable_audit_logging = true;
    size_t max_url_length = 2048;
    size_t max_metadata_length = 1024;
    size_t max_buffer_size = 1024 * 1024;  // 1MB
    size_t max_concurrent_connections = 100;
    std::string audit_log_path = "/var/log/odr-audioenc-audit.log";
    bool strict_ssl_verification = true;
    std::vector<std::string> allowed_url_schemes = {"http", "https", "icecast", "shoutcast"};
};

// Input validation utilities
class InputValidator {
private:
    SecurityConfig config_;
    
    // Validation patterns
    static const std::string url_pattern_;
    static const std::string metadata_pattern_;
    static const std::string filename_pattern_;
    
    // Character whitelists
    static const std::string safe_ascii_chars_;
    static const std::string safe_filename_chars_;

public:
    explicit InputValidator(const SecurityConfig& config = SecurityConfig{});
    
    // URL validation
    bool validate_stream_url(const std::string& url) const;
    bool validate_url_scheme(const std::string& scheme) const;
    bool validate_hostname(const std::string& hostname) const;
    bool validate_port(int port) const;
    
    // Metadata validation
    bool validate_metadata_field(const std::string& field) const;
    bool validate_metadata_length(const std::string& metadata) const;
    
    // File path validation
    bool validate_file_path(const std::string& path) const;
    bool validate_filename(const std::string& filename) const;
    bool is_path_traversal_attempt(const std::string& path) const;
    
    // General input validation
    bool validate_string_length(const std::string& input, size_t max_length) const;
    bool contains_only_safe_chars(const std::string& input, const std::string& allowed_chars) const;
    bool validate_utf8_encoding(const std::string& input) const;
    
    // Sanitization
    std::string sanitize_url(const std::string& url) const;
    std::string sanitize_metadata(const std::string& metadata) const;
    std::string sanitize_filename(const std::string& filename) const;
    std::string escape_html_entities(const std::string& input) const;
    std::string remove_control_characters(const std::string& input) const;
    
    // Configuration
    void update_config(const SecurityConfig& config) { config_ = config; }
    const SecurityConfig& get_config() const { return config_; }
};

// Buffer overflow protection
class SecureBuffer {
private:
    std::unique_ptr<uint8_t[]> buffer_;
    size_t capacity_;
    size_t size_;
    bool guard_enabled_;
    uint32_t guard_pattern_ = 0xDEADBEEF;
    
    // Guard bytes at the end of buffer
    static constexpr size_t GUARD_SIZE = 16;
    
    void write_guard_bytes();
    bool check_guard_bytes() const;

public:
    SecureBuffer(size_t capacity, bool enable_guard = true);
    ~SecureBuffer();
    
    // Buffer operations
    bool write(const void* data, size_t length);
    bool write_at(size_t offset, const void* data, size_t length);
    bool read(void* data, size_t length);
    bool read_from(size_t offset, void* data, size_t length);
    
    // Buffer management
    void clear();
    void resize(size_t new_capacity);
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    size_t available() const { return capacity_ - size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ >= capacity_; }
    
    // Security checks
    bool is_buffer_intact() const;
    void validate_buffer_integrity() const;
    
    // Raw access (use with caution)
    uint8_t* data() { return buffer_.get(); }
    const uint8_t* data() const { return buffer_.get(); }
};

// Memory management and leak detection
class MemoryManager {
private:
    struct AllocationInfo {
        size_t size;
        std::string file;
        int line;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::map<void*, AllocationInfo> allocations_;
    std::mutex allocations_mutex_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> peak_allocated_{0};
    std::atomic<size_t> allocation_count_{0};
    bool tracking_enabled_ = false;

public:
    MemoryManager();
    ~MemoryManager();
    
    // Memory allocation tracking
    void* allocate(size_t size, const std::string& file = "", int line = 0);
    void deallocate(void* ptr);
    
    // Memory statistics
    size_t get_total_allocated() const { return total_allocated_; }
    size_t get_peak_allocated() const { return peak_allocated_; }
    size_t get_allocation_count() const { return allocation_count_; }
    size_t get_active_allocations() const;
    
    // Leak detection
    std::vector<std::string> detect_memory_leaks() const;
    void report_memory_usage() const;
    void enable_tracking(bool enabled) { tracking_enabled_ = enabled; }
    
    // Memory pool for frequent allocations
    class MemoryPool {
    private:
        std::vector<std::unique_ptr<uint8_t[]>> blocks_;
        std::vector<void*> free_blocks_;
        size_t block_size_;
        size_t pool_size_;
        std::mutex pool_mutex_;

    public:
        MemoryPool(size_t block_size, size_t initial_blocks = 16);
        ~MemoryPool();
        
        void* allocate();
        void deallocate(void* ptr);
        size_t get_block_size() const { return block_size_; }
        size_t get_free_blocks() const { return free_blocks_.size(); }
    };
    
    std::unique_ptr<MemoryPool> create_pool(size_t block_size, size_t initial_blocks = 16);
    
    // Singleton instance
    static MemoryManager& instance();
};

// Audit logging system
class AuditLogger {
public:
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error,
        Security
    };
    
    enum class EventType {
        StreamConnection,
        ConfigurationChange,
        SecurityViolation,
        PerformanceAlert,
        ErrorEvent,
        SystemStart,
        SystemStop
    };

private:
    std::string log_file_path_;
    std::ofstream log_file_;
    std::mutex log_mutex_;
    LogLevel min_level_;
    bool enabled_;
    size_t max_file_size_ = 100 * 1024 * 1024; // 100MB
    size_t max_files_ = 5;
    
    void rotate_log_file();
    std::string format_log_entry(LogLevel level, EventType event, 
                                 const std::string& message, 
                                 const std::map<std::string, std::string>& context);

public:
    AuditLogger(const std::string& log_file_path, LogLevel min_level = LogLevel::Info);
    ~AuditLogger();
    
    // Logging methods
    void log(LogLevel level, EventType event, const std::string& message, 
             const std::map<std::string, std::string>& context = {});
    
    void debug(const std::string& message, const std::map<std::string, std::string>& context = {});
    void info(const std::string& message, const std::map<std::string, std::string>& context = {});
    void warning(const std::string& message, const std::map<std::string, std::string>& context = {});
    void error(const std::string& message, const std::map<std::string, std::string>& context = {});
    void security(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    // Event-specific logging
    void log_stream_connection(const std::string& url, bool success);
    void log_config_change(const std::string& parameter, const std::string& old_value, 
                          const std::string& new_value);
    void log_security_violation(const std::string& violation_type, const std::string& details);
    void log_performance_alert(const std::string& metric, double value, double threshold);
    
    // Configuration
    void set_min_level(LogLevel level) { min_level_ = level; }
    void enable(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }
};

// Performance monitoring and optimization
class PerformanceMonitor {
public:
    struct PerformanceMetrics {
        double cpu_usage_percent = 0.0;
        size_t memory_usage_bytes = 0;
        size_t peak_memory_bytes = 0;
        double audio_processing_latency_ms = 0.0;
        double network_latency_ms = 0.0;
        size_t buffer_underruns = 0;
        size_t buffer_overruns = 0;
        double throughput_mbps = 0.0;
        std::chrono::steady_clock::time_point last_updated;
    };
    
    struct PerformanceAlert {
        std::string metric_name;
        double current_value;
        double threshold;
        std::string description;
        std::chrono::steady_clock::time_point timestamp;
    };

private:
    PerformanceMetrics current_metrics_;
    std::vector<PerformanceAlert> active_alerts_;
    std::mutex metrics_mutex_;
    std::thread monitoring_thread_;
    std::atomic<bool> monitoring_enabled_{false};
    
    // Thresholds for alerts
    struct PerformanceThresholds {
        double max_cpu_usage = 80.0;           // 80% CPU
        size_t max_memory_usage = 512 * 1024 * 1024;  // 512MB
        double max_audio_latency = 50.0;       // 50ms
        double max_network_latency = 1000.0;   // 1s
        size_t max_buffer_underruns = 10;      // per minute
        double min_throughput = 1.0;           // 1 Mbps
    } thresholds_;
    
    // Performance optimization
    void optimize_cpu_usage();
    void optimize_memory_usage();
    void optimize_buffer_management();
    
    // Monitoring loop
    void monitoring_loop();
    void collect_system_metrics();
    void check_performance_thresholds();

public:
    PerformanceMonitor();
    ~PerformanceMonitor();
    
    // Monitoring control
    void start_monitoring();
    void stop_monitoring();
    bool is_monitoring() const { return monitoring_enabled_; }
    
    // Metrics collection
    void update_audio_latency(double latency_ms);
    void update_network_latency(double latency_ms);
    void record_buffer_underrun();
    void record_buffer_overrun();
    void update_throughput(double mbps);
    
    // Metrics access
    PerformanceMetrics get_current_metrics() const;
    std::vector<PerformanceAlert> get_active_alerts() const;
    void clear_alerts();
    
    // Thresholds configuration
    void set_cpu_threshold(double percent) { thresholds_.max_cpu_usage = percent; }
    void set_memory_threshold(size_t bytes) { thresholds_.max_memory_usage = bytes; }
    void set_audio_latency_threshold(double ms) { thresholds_.max_audio_latency = ms; }
    
    // Performance optimization triggers
    void trigger_cpu_optimization();
    void trigger_memory_optimization();
    void trigger_buffer_optimization();
};

// Multi-threading safety utilities
class ThreadSafeQueue {
private:
    std::vector<uint8_t> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

public:
    explicit ThreadSafeQueue(size_t capacity);
    
    // Queue operations
    bool push(const void* data, size_t length, 
              std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
    bool pop(void* data, size_t max_length, size_t& actual_length,
             std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
    
    // Queue status
    size_t size() const;
    size_t capacity() const { return capacity_; }
    bool empty() const;
    bool full() const;
    void clear();
    
    // Statistics
    size_t get_peak_size() const { return peak_size_; }
    size_t get_total_pushed() const { return total_pushed_; }
    size_t get_total_popped() const { return total_popped_; }

private:
    size_t peak_size_ = 0;
    size_t total_pushed_ = 0;
    size_t total_popped_ = 0;
};

// SIMD optimization utilities
class SIMDProcessor {
public:
    // Audio processing optimizations
    static void normalize_samples_simd(int16_t* samples, size_t count, float gain);
    static void mix_stereo_samples_simd(const int16_t* left, const int16_t* right, 
                                       int16_t* output, size_t count);
    static double calculate_rms_simd(const int16_t* samples, size_t count);
    static void apply_gain_simd(int16_t* samples, size_t count, float gain);
    
    // Memory operations
    static void secure_memcpy(void* dest, const void* src, size_t count);
    static void secure_memset(void* dest, int value, size_t count);
    
    // Capability detection
    static bool has_sse2_support();
    static bool has_avx2_support();
    static bool has_neon_support(); // ARM NEON
    
private:
    static bool cpu_capabilities_detected_;
    static bool has_sse2_;
    static bool has_avx2_;
    static bool has_neon_;
    
    static void detect_cpu_capabilities();
};

// Security exception handling
enum class SecurityViolationType {
    None,
    BufferOverflow,
    InvalidInput,
    PathTraversal,
    MemoryCorruption,
    UnauthorizedAccess,
    ResourceExhaustion
};

class SecurityException : public std::exception {
private:
    SecurityViolationType violation_type_;
    std::string message_;
    std::string context_;

public:
    SecurityException(SecurityViolationType type, const std::string& message, 
                     const std::string& context = "")
        : violation_type_(type), message_(message), context_(context) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    SecurityViolationType get_violation_type() const { return violation_type_; }
    const std::string& get_context() const { return context_; }
};

// Utility macros for memory safety
#define SECURE_ALLOC(size) StreamDAB::MemoryManager::instance().allocate(size, __FILE__, __LINE__)
#define SECURE_FREE(ptr) StreamDAB::MemoryManager::instance().deallocate(ptr)
#define VALIDATE_INPUT(condition, message) \
    if (!(condition)) throw StreamDAB::SecurityException( \
        StreamDAB::SecurityViolationType::InvalidInput, message, __FILE__ ":" + std::to_string(__LINE__))
#define CHECK_BUFFER_BOUNDS(ptr, size, max_size) \
    if (size > max_size) throw StreamDAB::SecurityException( \
        StreamDAB::SecurityViolationType::BufferOverflow, "Buffer size exceeds maximum", \
        __FILE__ ":" + std::to_string(__LINE__))

} // namespace StreamDAB