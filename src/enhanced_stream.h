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
#include <chrono>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "VLCInput.h"

namespace StreamDAB {

/*! \file enhanced_stream.h
 *  \brief Enhanced stream processing with improved VLC integration,
 *         automatic reconnection, and quality monitoring.
 */

struct StreamQualityMetrics {
    double snr_db = 0.0;
    double volume_peak = 0.0;
    double volume_rms = 0.0;
    int buffer_health = 100;  // 0-100%
    bool is_silence = false;
    std::chrono::steady_clock::time_point last_audio;
    size_t reconnect_count = 0;
    size_t underrun_count = 0;
    std::chrono::steady_clock::time_point start_time;
};

struct StreamConfig {
    std::string primary_url;
    std::vector<std::string> fallback_urls;
    int reconnect_delay_ms = 2000;
    int max_reconnects = 10;
    int buffer_ms = 5000;
    double silence_threshold_db = -40.0;
    int silence_timeout_s = 30;
    bool enable_normalization = true;
    double target_level_db = -23.0;  // EBU R128 standard
    std::string user_agent = "ODR-AudioEnc/StreamDAB Enhanced";
    bool verify_ssl = true;
    int connection_timeout_ms = 10000;
};

class EnhancedStreamProcessor {
private:
    StreamConfig config_;
    StreamQualityMetrics metrics_;
    std::unique_ptr<VLCInput> vlc_input_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<int> current_fallback_index_{-1};
    
    std::thread monitor_thread_;
    std::mutex metrics_mutex_;
    std::condition_variable reconnect_cv_;
    
    // Audio processing buffers
    std::vector<int16_t> audio_buffer_;
    std::vector<double> rms_history_;
    size_t rms_history_size_ = 100;
    
    // Normalization state
    double current_gain_ = 1.0;
    double target_gain_ = 1.0;
    double gain_smoothing_ = 0.001;  // Gain change rate
    
    void monitor_stream();
    bool attempt_connection(const std::string& url);
    void update_quality_metrics(const std::vector<int16_t>& samples);
    void apply_normalization(std::vector<int16_t>& samples);
    double calculate_rms(const std::vector<int16_t>& samples);
    double calculate_peak(const std::vector<int16_t>& samples);
    bool detect_silence(const std::vector<int16_t>& samples);
    void smooth_gain_transition();

public:
    EnhancedStreamProcessor(const StreamConfig& config);
    ~EnhancedStreamProcessor();
    
    // Core functionality
    bool initialize();
    bool start_stream();
    void stop_stream();
    bool is_running() const { return running_; }
    bool is_connected() const { return connected_; }
    
    // Audio data retrieval
    ssize_t get_samples(std::vector<int16_t>& samples, size_t max_samples);
    
    // Configuration management
    void update_config(const StreamConfig& config);
    const StreamConfig& get_config() const { return config_; }
    
    // Quality monitoring
    StreamQualityMetrics get_quality_metrics();
    void reset_metrics();
    
    // Stream management
    bool force_reconnect();
    void cycle_fallback();
    std::string get_current_url() const;
    
    // Metadata extraction
    std::string get_current_title() const;
    std::string get_current_artist() const;
    std::string get_stream_info() const;
    
    // Health checks
    bool is_healthy() const;
    std::vector<std::string> get_health_issues() const;
    
    // Statistics for monitoring
    struct StreamStats {
        size_t total_samples_processed = 0;
        size_t total_reconnects = 0;
        size_t total_buffer_underruns = 0;
        std::chrono::steady_clock::time_point uptime_start;
        double average_bitrate_kbps = 0.0;
        double current_latency_ms = 0.0;
    };
    
    StreamStats get_statistics() const;
};

// Utility functions for stream validation
namespace StreamUtils {
    bool validate_stream_url(const std::string& url);
    std::vector<std::string> detect_stream_format(const std::string& url);
    bool test_stream_connectivity(const std::string& url, int timeout_ms = 5000);
    std::string extract_metadata_from_response(const std::string& response);
    double measure_stream_latency(const std::string& url);
}

// Stream URL parser and validator
class StreamURLParser {
public:
    struct ParsedURL {
        std::string protocol;
        std::string hostname;
        int port = 80;
        std::string path;
        std::string query;
        std::string username;
        std::string password;
        bool is_valid = false;
    };
    
    static ParsedURL parse(const std::string& url);
    static bool is_supported_protocol(const std::string& protocol);
    static std::string sanitize_url(const std::string& url);
};

} // namespace StreamDAB