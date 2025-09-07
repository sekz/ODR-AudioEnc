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

#include "security_utils.h"
#include <regex>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>
#include <immintrin.h>  // For SIMD intrinsics
#include <arm_neon.h>   // For ARM NEON (conditionally compiled)

using namespace std;
using namespace std::chrono;

namespace StreamDAB {

// Static member initialization
const string InputValidator::url_pattern_ = 
    R"(^(https?|icecast|shoutcast)://[a-zA-Z0-9\-\._~:/?#[\]@!\$&'\(\)\*\+,;=%]+$)";
const string InputValidator::metadata_pattern_ = 
    R"(^[\x20-\x7E\u0E00-\u0E7F]*$)"; // ASCII + Thai Unicode block
const string InputValidator::filename_pattern_ = 
    R"(^[a-zA-Z0-9\-\._]+$)";
const string InputValidator::safe_ascii_chars_ = 
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
const string InputValidator::safe_filename_chars_ = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

// InputValidator implementation
InputValidator::InputValidator(const SecurityConfig& config) : config_(config) {}

bool InputValidator::validate_stream_url(const string& url) const {
    if (!config_.enable_input_validation) {
        return true;
    }
    
    // Check URL length
    if (url.length() > config_.max_url_length) {
        return false;
    }
    
    // Check for obvious malicious patterns
    if (url.find("javascript:") != string::npos ||
        url.find("data:") != string::npos ||
        url.find("<script") != string::npos) {
        return false;
    }
    
    // Validate against regex pattern
    regex url_regex(url_pattern_, regex_constants::icase);
    if (!regex_match(url, url_regex)) {
        return false;
    }
    
    // Extract and validate scheme
    size_t scheme_end = url.find("://");
    if (scheme_end != string::npos) {
        string scheme = url.substr(0, scheme_end);
        transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
        
        if (!validate_url_scheme(scheme)) {
            return false;
        }
    }
    
    return true;
}

bool InputValidator::validate_url_scheme(const string& scheme) const {
    return find(config_.allowed_url_schemes.begin(), 
                config_.allowed_url_schemes.end(), scheme) != 
           config_.allowed_url_schemes.end();
}

bool InputValidator::validate_hostname(const string& hostname) const {
    if (hostname.empty() || hostname.length() > 253) {
        return false;
    }
    
    // Check for IPv4 address
    regex ipv4_regex(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
    smatch ipv4_match;
    if (regex_match(hostname, ipv4_match, ipv4_regex)) {
        // Validate IPv4 octets
        for (int i = 1; i <= 4; ++i) {
            int octet = stoi(ipv4_match[i].str());
            if (octet > 255) {
                return false;
            }
        }
        return true;
    }
    
    // Check for valid hostname format
    regex hostname_regex(R"(^[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?)*$)");
    return regex_match(hostname, hostname_regex);
}

bool InputValidator::validate_port(int port) const {
    return port > 0 && port <= 65535;
}

bool InputValidator::validate_metadata_field(const string& field) const {
    if (!config_.enable_input_validation) {
        return true;
    }
    
    // Check length
    if (field.length() > config_.max_metadata_length) {
        return false;
    }
    
    // Validate UTF-8 encoding
    if (!validate_utf8_encoding(field)) {
        return false;
    }
    
    // Check for control characters (except tab, newline, carriage return)
    for (char c : field) {
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
    }
    
    return true;
}

bool InputValidator::validate_file_path(const string& path) const {
    if (!config_.enable_input_validation) {
        return true;
    }
    
    // Check for path traversal attempts
    if (is_path_traversal_attempt(path)) {
        return false;
    }
    
    // Check for null bytes
    if (path.find('\0') != string::npos) {
        return false;
    }
    
    // Validate path components
    istringstream path_stream(path);
    string component;
    while (getline(path_stream, component, '/')) {
        if (!component.empty() && !validate_filename(component)) {
            return false;
        }
    }
    
    return true;
}

bool InputValidator::is_path_traversal_attempt(const string& path) const {
    return path.find("../") != string::npos ||
           path.find("..\\") != string::npos ||
           path.find("/.") != string::npos ||
           path.find("\\.") != string::npos;
}

string InputValidator::sanitize_url(const string& url) const {
    string sanitized = url;
    
    // Remove any null bytes
    sanitized.erase(remove(sanitized.begin(), sanitized.end(), '\0'), sanitized.end());
    
    // Truncate if too long
    if (sanitized.length() > config_.max_url_length) {
        sanitized = sanitized.substr(0, config_.max_url_length);
    }
    
    return sanitized;
}

string InputValidator::sanitize_metadata(const string& metadata) const {
    string sanitized = metadata;
    
    // Remove control characters (except tab, newline, carriage return)
    sanitized.erase(remove_if(sanitized.begin(), sanitized.end(), 
                              [](char c) { return c < 32 && c != '\t' && c != '\n' && c != '\r'; }),
                    sanitized.end());
    
    // Truncate if too long
    if (sanitized.length() > config_.max_metadata_length) {
        sanitized = sanitized.substr(0, config_.max_metadata_length);
    }
    
    return sanitized;
}

bool InputValidator::validate_utf8_encoding(const string& input) const {
    // Simplified UTF-8 validation
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(input.c_str());
    size_t len = input.length();
    
    for (size_t i = 0; i < len; ) {
        unsigned char byte = bytes[i];
        
        if (byte <= 0x7F) {
            // ASCII character
            i++;
        }
        else if ((byte >> 5) == 0x06) {
            // 110xxxxx - 2 byte sequence
            if (i + 1 >= len || (bytes[i + 1] >> 6) != 0x02) {
                return false;
            }
            i += 2;
        }
        else if ((byte >> 4) == 0x0E) {
            // 1110xxxx - 3 byte sequence
            if (i + 2 >= len || (bytes[i + 1] >> 6) != 0x02 || (bytes[i + 2] >> 6) != 0x02) {
                return false;
            }
            i += 3;
        }
        else if ((byte >> 3) == 0x1E) {
            // 11110xxx - 4 byte sequence
            if (i + 3 >= len || (bytes[i + 1] >> 6) != 0x02 || 
                (bytes[i + 2] >> 6) != 0x02 || (bytes[i + 3] >> 6) != 0x02) {
                return false;
            }
            i += 4;
        }
        else {
            return false;
        }
    }
    
    return true;
}

// SecureBuffer implementation
SecureBuffer::SecureBuffer(size_t capacity, bool enable_guard) 
    : capacity_(capacity), size_(0), guard_enabled_(enable_guard) {
    
    size_t total_size = capacity_;
    if (guard_enabled_) {
        total_size += GUARD_SIZE;
    }
    
    buffer_ = make_unique<uint8_t[]>(total_size);
    
    if (guard_enabled_) {
        write_guard_bytes();
    }
}

SecureBuffer::~SecureBuffer() {
    if (guard_enabled_ && buffer_) {
        if (!check_guard_bytes()) {
            fprintf(stderr, "Buffer overflow detected in SecureBuffer destructor!\n");
        }
    }
}

void SecureBuffer::write_guard_bytes() {
    if (!guard_enabled_ || !buffer_) return;
    
    uint8_t* guard_area = buffer_.get() + capacity_;
    uint32_t* guard_words = reinterpret_cast<uint32_t*>(guard_area);
    
    for (size_t i = 0; i < GUARD_SIZE / sizeof(uint32_t); ++i) {
        guard_words[i] = guard_pattern_;
    }
}

bool SecureBuffer::check_guard_bytes() const {
    if (!guard_enabled_ || !buffer_) return true;
    
    const uint8_t* guard_area = buffer_.get() + capacity_;
    const uint32_t* guard_words = reinterpret_cast<const uint32_t*>(guard_area);
    
    for (size_t i = 0; i < GUARD_SIZE / sizeof(uint32_t); ++i) {
        if (guard_words[i] != guard_pattern_) {
            return false;
        }
    }
    
    return true;
}

bool SecureBuffer::write(const void* data, size_t length) {
    if (!buffer_ || length > (capacity_ - size_)) {
        return false;
    }
    
    memcpy(buffer_.get() + size_, data, length);
    size_ += length;
    
    return true;
}

bool SecureBuffer::write_at(size_t offset, const void* data, size_t length) {
    if (!buffer_ || offset + length > capacity_) {
        return false;
    }
    
    memcpy(buffer_.get() + offset, data, length);
    size_ = max(size_, offset + length);
    
    return true;
}

void SecureBuffer::validate_buffer_integrity() const {
    if (guard_enabled_ && !check_guard_bytes()) {
        throw SecurityException(SecurityViolationType::BufferOverflow, 
                               "Buffer overflow detected - guard bytes corrupted");
    }
}

// MemoryManager implementation
MemoryManager::MemoryManager() {
    tracking_enabled_ = true;
}

MemoryManager::~MemoryManager() {
    if (tracking_enabled_) {
        auto leaks = detect_memory_leaks();
        if (!leaks.empty()) {
            fprintf(stderr, "Memory leaks detected:\n");
            for (const auto& leak : leaks) {
                fprintf(stderr, "  %s\n", leak.c_str());
            }
        }
    }
}

void* MemoryManager::allocate(size_t size, const string& file, int line) {
    void* ptr = malloc(size);
    if (!ptr) {
        throw bad_alloc();
    }
    
    if (tracking_enabled_) {
        lock_guard<mutex> lock(allocations_mutex_);
        allocations_[ptr] = {size, file, line, steady_clock::now()};
        
        total_allocated_ += size;
        peak_allocated_ = max(peak_allocated_.load(), total_allocated_.load());
        allocation_count_++;
    }
    
    return ptr;
}

void MemoryManager::deallocate(void* ptr) {
    if (!ptr) return;
    
    if (tracking_enabled_) {
        lock_guard<mutex> lock(allocations_mutex_);
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            total_allocated_ -= it->second.size;
            allocations_.erase(it);
        }
    }
    
    free(ptr);
}

vector<string> MemoryManager::detect_memory_leaks() const {
    vector<string> leaks;
    
    if (tracking_enabled_) {
        lock_guard<mutex> lock(allocations_mutex_);
        for (const auto& allocation : allocations_) {
            ostringstream oss;
            oss << "Leaked " << allocation.second.size << " bytes allocated at " 
                << allocation.second.file << ":" << allocation.second.line;
            leaks.push_back(oss.str());
        }
    }
    
    return leaks;
}

MemoryManager& MemoryManager::instance() {
    static MemoryManager instance;
    return instance;
}

// AuditLogger implementation
AuditLogger::AuditLogger(const string& log_file_path, LogLevel min_level) 
    : log_file_path_(log_file_path), min_level_(min_level), enabled_(true) {
    
    log_file_.open(log_file_path_, ios::app);
    if (!log_file_) {
        fprintf(stderr, "Failed to open audit log file: %s\n", log_file_path_.c_str());
        enabled_ = false;
    }
}

AuditLogger::~AuditLogger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void AuditLogger::log(LogLevel level, EventType event, const string& message, 
                      const map<string, string>& context) {
    if (!enabled_ || level < min_level_) {
        return;
    }
    
    lock_guard<mutex> lock(log_mutex_);
    
    string formatted_entry = format_log_entry(level, event, message, context);
    log_file_ << formatted_entry << endl;
    log_file_.flush();
    
    // Check if log rotation is needed
    if (log_file_.tellp() > static_cast<streampos>(max_file_size_)) {
        rotate_log_file();
    }
}

string AuditLogger::format_log_entry(LogLevel level, EventType event, 
                                     const string& message, 
                                     const map<string, string>& context) {
    ostringstream oss;
    
    // Timestamp
    auto now = system_clock::now();
    time_t now_t = system_clock::to_time_t(now);
    oss << put_time(gmtime(&now_t), "%Y-%m-%dT%H:%M:%SZ");
    
    // Log level
    oss << " [";
    switch (level) {
        case LogLevel::Debug: oss << "DEBUG"; break;
        case LogLevel::Info: oss << "INFO"; break;
        case LogLevel::Warning: oss << "WARN"; break;
        case LogLevel::Error: oss << "ERROR"; break;
        case LogLevel::Security: oss << "SECURITY"; break;
    }
    oss << "]";
    
    // Event type
    oss << " [";
    switch (event) {
        case EventType::StreamConnection: oss << "STREAM_CONNECTION"; break;
        case EventType::ConfigurationChange: oss << "CONFIG_CHANGE"; break;
        case EventType::SecurityViolation: oss << "SECURITY_VIOLATION"; break;
        case EventType::PerformanceAlert: oss << "PERFORMANCE_ALERT"; break;
        case EventType::ErrorEvent: oss << "ERROR_EVENT"; break;
        case EventType::SystemStart: oss << "SYSTEM_START"; break;
        case EventType::SystemStop: oss << "SYSTEM_STOP"; break;
    }
    oss << "]";
    
    // Message
    oss << " " << message;
    
    // Context
    if (!context.empty()) {
        oss << " {";
        bool first = true;
        for (const auto& kv : context) {
            if (!first) oss << ", ";
            oss << kv.first << "='" << kv.second << "'";
            first = false;
        }
        oss << "}";
    }
    
    return oss.str();
}

void AuditLogger::security(const string& message, const map<string, string>& context) {
    log(LogLevel::Security, EventType::SecurityViolation, message, context);
}

// PerformanceMonitor implementation
PerformanceMonitor::PerformanceMonitor() {
    current_metrics_.last_updated = steady_clock::now();
}

PerformanceMonitor::~PerformanceMonitor() {
    stop_monitoring();
}

void PerformanceMonitor::start_monitoring() {
    if (monitoring_enabled_) {
        return;
    }
    
    monitoring_enabled_ = true;
    monitoring_thread_ = thread(&PerformanceMonitor::monitoring_loop, this);
}

void PerformanceMonitor::stop_monitoring() {
    monitoring_enabled_ = false;
    
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void PerformanceMonitor::monitoring_loop() {
    while (monitoring_enabled_) {
        collect_system_metrics();
        check_performance_thresholds();
        
        this_thread::sleep_for(seconds(1));
    }
}

void PerformanceMonitor::collect_system_metrics() {
    lock_guard<mutex> lock(metrics_mutex_);
    
    // Get memory usage
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        current_metrics_.memory_usage_bytes = usage.ru_maxrss * 1024; // Convert to bytes
        current_metrics_.peak_memory_bytes = max(current_metrics_.peak_memory_bytes, 
                                                current_metrics_.memory_usage_bytes);
    }
    
    // CPU usage would require more complex calculation
    // For now, set a placeholder value
    current_metrics_.cpu_usage_percent = 0.0;
    
    current_metrics_.last_updated = steady_clock::now();
}

void PerformanceMonitor::check_performance_thresholds() {
    vector<PerformanceAlert> new_alerts;
    
    {
        lock_guard<mutex> lock(metrics_mutex_);
        
        // Check CPU usage
        if (current_metrics_.cpu_usage_percent > thresholds_.max_cpu_usage) {
            PerformanceAlert alert;
            alert.metric_name = "cpu_usage";
            alert.current_value = current_metrics_.cpu_usage_percent;
            alert.threshold = thresholds_.max_cpu_usage;
            alert.description = "CPU usage exceeds threshold";
            alert.timestamp = steady_clock::now();
            new_alerts.push_back(alert);
        }
        
        // Check memory usage
        if (current_metrics_.memory_usage_bytes > thresholds_.max_memory_usage) {
            PerformanceAlert alert;
            alert.metric_name = "memory_usage";
            alert.current_value = static_cast<double>(current_metrics_.memory_usage_bytes);
            alert.threshold = static_cast<double>(thresholds_.max_memory_usage);
            alert.description = "Memory usage exceeds threshold";
            alert.timestamp = steady_clock::now();
            new_alerts.push_back(alert);
        }
        
        // Check audio latency
        if (current_metrics_.audio_processing_latency_ms > thresholds_.max_audio_latency) {
            PerformanceAlert alert;
            alert.metric_name = "audio_latency";
            alert.current_value = current_metrics_.audio_processing_latency_ms;
            alert.threshold = thresholds_.max_audio_latency;
            alert.description = "Audio processing latency exceeds threshold";
            alert.timestamp = steady_clock::now();
            new_alerts.push_back(alert);
        }
    }
    
    // Add new alerts
    for (const auto& alert : new_alerts) {
        active_alerts_.push_back(alert);
        
        // Log performance alert
        // This would integrate with AuditLogger
    }
}

// SIMD processor implementation
bool SIMDProcessor::cpu_capabilities_detected_ = false;
bool SIMDProcessor::has_sse2_ = false;
bool SIMDProcessor::has_avx2_ = false;
bool SIMDProcessor::has_neon_ = false;

void SIMDProcessor::detect_cpu_capabilities() {
    if (cpu_capabilities_detected_) return;
    
#ifdef __x86_64__
    // x86-64 CPU capability detection
    uint32_t eax, ebx, ecx, edx;
    
    // Check for SSE2 support
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    has_sse2_ = (edx & (1 << 26)) != 0;
    
    // Check for AVX2 support
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    has_avx2_ = (ebx & (1 << 5)) != 0;
#endif

#ifdef __ARM_NEON__
    has_neon_ = true;
#endif
    
    cpu_capabilities_detected_ = true;
}

void SIMDProcessor::normalize_samples_simd(int16_t* samples, size_t count, float gain) {
    detect_cpu_capabilities();
    
#ifdef __x86_64__
    if (has_sse2_ && count >= 8) {
        __m128 gain_vec = _mm_set1_ps(gain);
        size_t simd_count = count & ~7; // Process in chunks of 8
        
        for (size_t i = 0; i < simd_count; i += 8) {
            // Load 8 int16_t samples
            __m128i samples_i16 = _mm_loadu_si128(reinterpret_cast<__m128i*>(&samples[i]));
            
            // Convert to float (lower 4 samples)
            __m128i samples_low = _mm_unpacklo_epi16(samples_i16, _mm_setzero_si128());
            __m128 samples_f_low = _mm_cvtepi32_ps(_mm_unpacklo_epi16(samples_low, _mm_setzero_si128()));
            
            // Apply gain
            samples_f_low = _mm_mul_ps(samples_f_low, gain_vec);
            
            // Convert back to int16_t and store
            __m128i result_low = _mm_cvtps_epi32(samples_f_low);
            result_low = _mm_packs_epi32(result_low, result_low);
            
            _mm_storel_epi64(reinterpret_cast<__m128i*>(&samples[i]), result_low);
        }
        
        // Process remaining samples
        for (size_t i = simd_count; i < count; ++i) {
            samples[i] = static_cast<int16_t>(samples[i] * gain);
        }
        return;
    }
#endif

    // Fallback to scalar implementation
    for (size_t i = 0; i < count; ++i) {
        samples[i] = static_cast<int16_t>(samples[i] * gain);
    }
}

double SIMDProcessor::calculate_rms_simd(const int16_t* samples, size_t count) {
    detect_cpu_capabilities();
    
    if (count == 0) return 0.0;
    
    double sum_squares = 0.0;
    
#ifdef __x86_64__
    if (has_sse2_ && count >= 8) {
        __m128d sum_vec = _mm_setzero_pd();
        size_t simd_count = count & ~7;
        
        for (size_t i = 0; i < simd_count; i += 8) {
            __m128i samples_i16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&samples[i]));
            
            // Convert to 32-bit and then to double for better precision
            __m128i samples_low = _mm_unpacklo_epi16(samples_i16, _mm_setzero_si128());
            __m128d samples_d_low = _mm_cvtepi32_pd(samples_low);
            
            // Square and accumulate
            samples_d_low = _mm_mul_pd(samples_d_low, samples_d_low);
            sum_vec = _mm_add_pd(sum_vec, samples_d_low);
        }
        
        // Extract sum from vector
        double temp[2];
        _mm_storeu_pd(temp, sum_vec);
        sum_squares = temp[0] + temp[1];
        
        // Process remaining samples
        for (size_t i = simd_count; i < count; ++i) {
            double sample = samples[i];
            sum_squares += sample * sample;
        }
    }
    else
#endif
    {
        // Scalar fallback
        for (size_t i = 0; i < count; ++i) {
            double sample = samples[i];
            sum_squares += sample * sample;
        }
    }
    
    return sqrt(sum_squares / count);
}

bool SIMDProcessor::has_sse2_support() {
    detect_cpu_capabilities();
    return has_sse2_;
}

bool SIMDProcessor::has_avx2_support() {
    detect_cpu_capabilities();
    return has_avx2_;
}

bool SIMDProcessor::has_neon_support() {
    detect_cpu_capabilities();
    return has_neon_;
}

} // namespace StreamDAB