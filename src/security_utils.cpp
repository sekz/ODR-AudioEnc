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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>
#include <immintrin.h>  // For SIMD intrinsics (x86/x64)

// Conditionally include ARM NEON only on ARM architectures
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>   // For ARM NEON (conditionally compiled)
#endif

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
    // Compiled once: constructing a std::regex on every call is very slow
    static const regex url_regex(url_pattern_, regex_constants::icase);
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
    static const regex ipv4_regex(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
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
    
    // A name whose last label is purely numeric is a malformed IPv4 address, not a hostname
    const string last_label = hostname.substr(hostname.rfind('.') == string::npos ? 0 : hostname.rfind('.') + 1);
    if (!last_label.empty() and
            all_of(last_label.begin(), last_label.end(), [](unsigned char c) { return isdigit(c); })) {
        return false;
    }

    // Check for valid hostname format
    static const regex hostname_regex(R"(^[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?)*$)");
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
    for (unsigned char c : field) {
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

bool InputValidator::validate_filename(const string& filename) const {
    if (filename.empty() or filename.size() > 255) {
        return false;
    }
    if (filename == "." or filename == "..") {
        return false;
    }
    return filename.find_first_not_of(safe_filename_chars_) == string::npos;
}

string InputValidator::sanitize_url(const string& url) const {
    string sanitized = url;
    
    // Remove null bytes and other control characters
    sanitized.erase(remove_if(sanitized.begin(), sanitized.end(),
                              [](unsigned char c) { return c < 32 or c == 127; }),
                    sanitized.end());
    
    // Truncate if too long
    if (sanitized.length() > config_.max_url_length) {
        sanitized = sanitized.substr(0, config_.max_url_length);
    }

    // Percent-encode characters that must not appear raw in a URL (markup, quotes,
    // spaces and non-ASCII bytes), so a hostile URL cannot carry HTML/script.
    static const string unsafe = " \"<>\\^`{|}";
    string encoded;
    encoded.reserve(sanitized.size());
    for (unsigned char c : sanitized) {
        if (c >= 128 or unsafe.find(static_cast<char>(c)) != string::npos) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
        else {
            encoded += static_cast<char>(c);
        }
    }

    return encoded;
}

string InputValidator::sanitize_metadata(const string& metadata) const {
    string sanitized = metadata;
    
    // Remove control characters (except tab, newline, carriage return)
    sanitized.erase(remove_if(sanitized.begin(), sanitized.end(), 
                              [](unsigned char c) { return c < 32 && c != '\t' && c != '\n' && c != '\r'; }),
                    sanitized.end());
    
    // Truncate if too long, without cutting a multi-byte UTF-8 sequence in half
    if (sanitized.length() > config_.max_metadata_length) {
        size_t cut = config_.max_metadata_length;
        while (cut > 0 and (static_cast<unsigned char>(sanitized[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        sanitized.resize(cut);
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

bool SecureBuffer::read(void* data, size_t length) {
    return read_from(0, data, length);
}

bool SecureBuffer::read_from(size_t offset, void* data, size_t length) {
    if (!buffer_ or offset > size_ or length > size_ - offset) {
        return false;
    }

    memcpy(data, buffer_.get() + offset, length);
    return true;
}

void SecureBuffer::clear() {
    if (buffer_) {
        memset(buffer_.get(), 0, capacity_);
    }
    size_ = 0;
}

void SecureBuffer::resize(size_t new_capacity) {
    const size_t total_size = new_capacity + (guard_enabled_ ? GUARD_SIZE : 0);
    auto new_buffer = make_unique<uint8_t[]>(total_size);

    if (buffer_) {
        memcpy(new_buffer.get(), buffer_.get(), min(size_, new_capacity));
    }

    buffer_ = move(new_buffer);
    capacity_ = new_capacity;
    size_ = min(size_, new_capacity);
    write_guard_bytes();
}

bool SecureBuffer::is_buffer_intact() const {
    return check_guard_bytes();
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

size_t MemoryManager::get_active_allocations() const {
    lock_guard<mutex> lock(allocations_mutex_);
    return allocations_.size();
}

MemoryManager::MemoryPool::MemoryPool(size_t block_size, size_t initial_blocks)
    : block_size_(block_size), pool_size_(initial_blocks) {

    blocks_.reserve(initial_blocks);
    free_blocks_.reserve(initial_blocks);
    for (size_t i = 0; i < initial_blocks; ++i) {
        blocks_.push_back(make_unique<uint8_t[]>(block_size_));
        free_blocks_.push_back(blocks_.back().get());
    }
}

MemoryManager::MemoryPool::~MemoryPool() = default;

void* MemoryManager::MemoryPool::allocate() {
    lock_guard<mutex> lock(pool_mutex_);
    if (free_blocks_.empty()) {
        return nullptr;
    }

    void* block = free_blocks_.back();
    free_blocks_.pop_back();
    return block;
}

void MemoryManager::MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;

    lock_guard<mutex> lock(pool_mutex_);

    // Only accept blocks that belong to this pool and are not already free
    const bool owned = any_of(blocks_.begin(), blocks_.end(),
            [ptr](const unique_ptr<uint8_t[]>& b) { return b.get() == ptr; });
    const bool already_free = find(free_blocks_.begin(), free_blocks_.end(), ptr) != free_blocks_.end();
    if (owned and not already_free) {
        free_blocks_.push_back(ptr);
    }
}

unique_ptr<MemoryManager::MemoryPool> MemoryManager::create_pool(size_t block_size, size_t initial_blocks) {
    return make_unique<MemoryPool>(block_size, initial_blocks);
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

void AuditLogger::rotate_log_file() {
    // Called with log_mutex_ held. Shifts <path> -> <path>.1 -> <path>.2 ... and
    // drops the oldest file.
    log_file_.close();

    remove((log_file_path_ + "." + to_string(max_files_ - 1)).c_str());
    for (size_t i = max_files_ - 1; i > 1; --i) {
        rename((log_file_path_ + "." + to_string(i - 1)).c_str(),
               (log_file_path_ + "." + to_string(i)).c_str());
    }
    if (max_files_ > 1) {
        rename(log_file_path_.c_str(), (log_file_path_ + ".1").c_str());
    }
    else {
        remove(log_file_path_.c_str());
    }

    log_file_.open(log_file_path_, ios::app);
    if (!log_file_) {
        fprintf(stderr, "Failed to reopen audit log file: %s\n", log_file_path_.c_str());
        enabled_ = false;
    }
}

void AuditLogger::debug(const string& message, const map<string, string>& context) {
    log(LogLevel::Debug, EventType::SystemStart, message, context);
}

void AuditLogger::info(const string& message, const map<string, string>& context) {
    log(LogLevel::Info, EventType::SystemStart, message, context);
}

void AuditLogger::warning(const string& message, const map<string, string>& context) {
    log(LogLevel::Warning, EventType::ErrorEvent, message, context);
}

void AuditLogger::error(const string& message, const map<string, string>& context) {
    log(LogLevel::Error, EventType::ErrorEvent, message, context);
}

void AuditLogger::log_stream_connection(const string& url, bool success) {
    log(success ? LogLevel::Info : LogLevel::Warning, EventType::StreamConnection,
        success ? "Stream connection established" : "Stream connection failed",
        {{"url", url}});
}

void AuditLogger::log_config_change(const string& parameter, const string& old_value,
                                    const string& new_value) {
    log(LogLevel::Info, EventType::ConfigurationChange, "Configuration changed",
        {{"parameter", parameter}, {"old_value", old_value}, {"new_value", new_value}});
}

void AuditLogger::log_security_violation(const string& violation_type, const string& details) {
    log(LogLevel::Security, EventType::SecurityViolation, violation_type,
        {{"details", details}});
}

void AuditLogger::log_performance_alert(const string& metric, double value, double threshold) {
    log(LogLevel::Warning, EventType::PerformanceAlert, "Performance threshold exceeded",
        {{"metric", metric}, {"value", to_string(value)}, {"threshold", to_string(threshold)}});
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

void PerformanceMonitor::update_audio_latency(double latency_ms) {
    lock_guard<mutex> lock(metrics_mutex_);
    current_metrics_.audio_processing_latency_ms = latency_ms;
    current_metrics_.last_updated = steady_clock::now();
}

void PerformanceMonitor::update_network_latency(double latency_ms) {
    lock_guard<mutex> lock(metrics_mutex_);
    current_metrics_.network_latency_ms = latency_ms;
    current_metrics_.last_updated = steady_clock::now();
}

void PerformanceMonitor::record_buffer_underrun() {
    lock_guard<mutex> lock(metrics_mutex_);
    current_metrics_.buffer_underruns++;
    current_metrics_.last_updated = steady_clock::now();
}

void PerformanceMonitor::record_buffer_overrun() {
    lock_guard<mutex> lock(metrics_mutex_);
    current_metrics_.buffer_overruns++;
    current_metrics_.last_updated = steady_clock::now();
}

void PerformanceMonitor::update_throughput(double mbps) {
    lock_guard<mutex> lock(metrics_mutex_);
    current_metrics_.throughput_mbps = mbps;
    current_metrics_.last_updated = steady_clock::now();
}

PerformanceMonitor::PerformanceMetrics PerformanceMonitor::get_current_metrics() const {
    lock_guard<mutex> lock(metrics_mutex_);
    return current_metrics_;
}

vector<PerformanceMonitor::PerformanceAlert> PerformanceMonitor::get_active_alerts() const {
    lock_guard<mutex> lock(metrics_mutex_);
    return active_alerts_;
}

void PerformanceMonitor::clear_alerts() {
    lock_guard<mutex> lock(metrics_mutex_);
    active_alerts_.clear();
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
    lock_guard<mutex> lock(metrics_mutex_);
    for (const auto& alert : new_alerts) {
        active_alerts_.push_back(alert);
        
        // Log performance alert
        // This would integrate with AuditLogger
    }
}

// ThreadSafeQueue implementation
ThreadSafeQueue::ThreadSafeQueue(size_t capacity) : capacity_(capacity) {}

bool ThreadSafeQueue::push(const void* data, size_t length, milliseconds timeout) {
    if (!data and length > 0) {
        return false;
    }
    if (length > capacity_) {
        return false; // can never fit
    }

    unique_lock<mutex> lock(mutex_);
    if (!not_full_.wait_for(lock, timeout, [&] { return capacity_ - size_ >= length; })) {
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    messages_.emplace_back(bytes, bytes + length);
    size_ += length;
    total_pushed_ += length;
    peak_size_ = max(peak_size_, size_);

    not_empty_.notify_one();
    return true;
}

bool ThreadSafeQueue::pop(void* data, size_t max_length, size_t& actual_length, milliseconds timeout) {
    unique_lock<mutex> lock(mutex_);
    if (!not_empty_.wait_for(lock, timeout, [&] { return !messages_.empty(); })) {
        return false;
    }

    // A message that does not fit is left in the queue rather than truncated
    const vector<uint8_t>& front = messages_.front();
    if (front.size() > max_length) {
        return false;
    }

    actual_length = front.size();
    if (actual_length > 0) {
        memcpy(data, front.data(), actual_length);
    }
    size_ -= actual_length;
    total_popped_ += actual_length;
    messages_.pop_front();

    not_full_.notify_one();
    return true;
}

size_t ThreadSafeQueue::size() const {
    lock_guard<mutex> lock(mutex_);
    return size_;
}

bool ThreadSafeQueue::empty() const {
    lock_guard<mutex> lock(mutex_);
    return messages_.empty();
}

bool ThreadSafeQueue::full() const {
    lock_guard<mutex> lock(mutex_);
    return size_ >= capacity_;
}

void ThreadSafeQueue::clear() {
    lock_guard<mutex> lock(mutex_);
    messages_.clear();
    size_ = 0;
    not_full_.notify_all();
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

static inline int16_t scale_sample(int16_t sample, float gain) {
    const float scaled = static_cast<float>(sample) * gain;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(scaled);
}

void SIMDProcessor::normalize_samples_simd(int16_t* samples, size_t count, float gain) {
    detect_cpu_capabilities();

    size_t done = 0;

#ifdef __x86_64__
    if (has_sse2_ && count >= 8) {
        const __m128 gain_vec = _mm_set1_ps(gain);
        const size_t simd_count = count & ~size_t(7); // Process in chunks of 8

        for (; done < simd_count; done += 8) {
            const __m128i s16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&samples[done]));

            // Sign-extend int16 -> int32 (SSE2): duplicate into both halves, then arithmetic shift
            const __m128i lo32 = _mm_srai_epi32(_mm_unpacklo_epi16(s16, s16), 16);
            const __m128i hi32 = _mm_srai_epi32(_mm_unpackhi_epi16(s16, s16), 16);

            const __m128 lo_f = _mm_mul_ps(_mm_cvtepi32_ps(lo32), gain_vec);
            const __m128 hi_f = _mm_mul_ps(_mm_cvtepi32_ps(hi32), gain_vec);

            // _mm_packs_epi32 saturates to the int16 range
            const __m128i result = _mm_packs_epi32(_mm_cvttps_epi32(lo_f), _mm_cvttps_epi32(hi_f));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&samples[done]), result);
        }
    }
#endif

    // Remaining samples (or everything, without SSE2)
    for (; done < count; ++done) {
        samples[done] = scale_sample(samples[done], gain);
    }
}

void SIMDProcessor::apply_gain_simd(int16_t* samples, size_t count, float gain) {
    normalize_samples_simd(samples, count, gain);
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