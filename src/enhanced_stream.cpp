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

#include "enhanced_stream.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <cmath>
#include <regex>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>

using namespace std;
using namespace std::chrono;

namespace StreamDAB {

EnhancedStreamProcessor::EnhancedStreamProcessor(const StreamConfig& config) 
    : config_(config) {
    audio_buffer_.reserve(48000 * 2);  // 1 second of stereo at 48kHz
    rms_history_.reserve(rms_history_size_);
    
    metrics_.start_time = steady_clock::now();
    metrics_.last_audio = steady_clock::now();
}

EnhancedStreamProcessor::~EnhancedStreamProcessor() {
    stop_stream();
}

bool EnhancedStreamProcessor::initialize() {
    try {
        // Initialize VLC input with enhanced configuration
        vlc_input_ = make_unique<VLCInput>(config_.primary_url, 
                                          48000, // Sample rate
                                          2,     // Channels
                                          config_.buffer_ms);
        
        // Set additional VLC options for better streaming
        vector<string> vlc_options = {
            "--intf=dummy",
            "--extraintf=",
            "--network-caching=" + to_string(config_.buffer_ms),
            "--clock-jitter=0",
            "--clock-synchro=0",
            "--http-user-agent=" + config_.user_agent,
            "--http-reconnect",
            "--sout-keep"
        };
        
        if (!config_.verify_ssl) {
            vlc_options.push_back("--http-no-ssl-verify");
        }
        
        return vlc_input_->initialize(vlc_options);
    }
    catch (const exception& e) {
        fprintf(stderr, "Enhanced Stream Processor initialization failed: %s\n", e.what());
        return false;
    }
}

bool EnhancedStreamProcessor::start_stream() {
    if (running_) {
        return true;
    }
    
    running_ = true;
    
    // Start with primary URL
    current_fallback_index_ = -1;
    
    if (attempt_connection(config_.primary_url)) {
        connected_ = true;
        
        // Start monitoring thread
        monitor_thread_ = thread(&EnhancedStreamProcessor::monitor_stream, this);
        
        return true;
    }
    
    // Try fallback URLs
    for (size_t i = 0; i < config_.fallback_urls.size(); ++i) {
        if (attempt_connection(config_.fallback_urls[i])) {
            connected_ = true;
            current_fallback_index_ = static_cast<int>(i);
            
            monitor_thread_ = thread(&EnhancedStreamProcessor::monitor_stream, this);
            return true;
        }
    }
    
    running_ = false;
    return false;
}

void EnhancedStreamProcessor::stop_stream() {
    running_ = false;
    connected_ = false;
    
    if (monitor_thread_.joinable()) {
        reconnect_cv_.notify_all();
        monitor_thread_.join();
    }
    
    if (vlc_input_) {
        vlc_input_.reset();
    }
}

bool EnhancedStreamProcessor::attempt_connection(const string& url) {
    try {
        if (!vlc_input_) {
            return false;
        }
        
        // Test connectivity first
        if (!StreamUtils::test_stream_connectivity(url, config_.connection_timeout_ms)) {
            fprintf(stderr, "Stream connectivity test failed for: %s\n", url.c_str());
            return false;
        }
        
        // Attempt VLC connection
        if (vlc_input_->open(url)) {
            printf("Successfully connected to stream: %s\n", url.c_str());
            
            // Update metrics
            lock_guard<mutex> lock(metrics_mutex_);
            metrics_.reconnect_count++;
            metrics_.last_audio = steady_clock::now();
            
            return true;
        }
        
        return false;
    }
    catch (const exception& e) {
        fprintf(stderr, "Connection attempt failed for %s: %s\n", url.c_str(), e.what());
        return false;
    }
}

void EnhancedStreamProcessor::monitor_stream() {
    while (running_) {
        if (!connected_) {
            // Try to reconnect
            bool reconnected = false;
            
            // Try primary URL first
            if (current_fallback_index_ != -1) {
                if (attempt_connection(config_.primary_url)) {
                    current_fallback_index_ = -1;
                    connected_ = true;
                    reconnected = true;
                }
            }
            
            // Try current or next fallback
            if (!reconnected && !config_.fallback_urls.empty()) {
                int next_index = (current_fallback_index_ + 1) % static_cast<int>(config_.fallback_urls.size());
                if (attempt_connection(config_.fallback_urls[next_index])) {
                    current_fallback_index_ = next_index;
                    connected_ = true;
                    reconnected = true;
                }
            }
            
            if (!reconnected) {
                // Wait before next attempt
                unique_lock<mutex> lock(metrics_mutex_);
                reconnect_cv_.wait_for(lock, milliseconds(config_.reconnect_delay_ms));
                continue;
            }
        }
        
        // Check stream health
        steady_clock::time_point last_audio;
        int silence_timeout_s;
        {
            lock_guard<mutex> lock(metrics_mutex_);
            last_audio = metrics_.last_audio;
            silence_timeout_s = config_.silence_timeout_s;
        }
        auto silence_duration = duration_cast<seconds>(steady_clock::now() - last_audio).count();
        
        if (silence_duration > silence_timeout_s) {
            printf("Stream silence timeout, attempting reconnection...\n");
            connected_ = false;
            continue;
        }
        
        // Monitor buffer health
        if (vlc_input_) {
            // This would need to be implemented in VLCInput
            // int buffer_health = vlc_input_->getBufferHealth();
            // if (buffer_health < 20) { // Less than 20% buffer
            //     metrics_.underrun_count++;
            // }
        }
        
        // Sleep for monitoring interval
        this_thread::sleep_for(milliseconds(1000));
    }
}

ssize_t EnhancedStreamProcessor::get_samples(vector<int16_t>& samples, size_t max_samples) {
    if (!vlc_input_ || !connected_) {
        return 0;
    }
    
    samples.resize(max_samples);
    ssize_t samples_read = vlc_input_->read(samples.data(), max_samples);
    
    if (samples_read > 0) {
        samples.resize(samples_read);
        
        // Update quality metrics
        update_quality_metrics(samples);
        
        // Apply normalization if enabled
        if (config_.enable_normalization) {
            apply_normalization(samples);
        }
        
        // Update last audio timestamp and sample counter
        lock_guard<mutex> lock(metrics_mutex_);
        metrics_.last_audio = steady_clock::now();
        total_samples_ += static_cast<size_t>(samples_read);
    }
    else if (samples_read < 0) {
        // Error occurred, mark as disconnected
        connected_ = false;
    }
    
    return samples_read;
}

void EnhancedStreamProcessor::update_quality_metrics(const vector<int16_t>& samples) {
    if (samples.empty()) return;
    
    lock_guard<mutex> lock(metrics_mutex_);
    
    // Calculate RMS and peak
    double rms = calculate_rms(samples);
    double peak = calculate_peak(samples);
    
    metrics_.volume_rms = rms;
    metrics_.volume_peak = peak;
    
    // Update RMS history for trend analysis
    rms_history_.push_back(rms);
    if (rms_history_.size() > rms_history_size_) {
        rms_history_.erase(rms_history_.begin());
    }
    
    // Detect silence
    metrics_.is_silence = detect_silence(samples);
    
    // Calculate SNR (simplified approximation)
    if (rms > 0.001) {
        double noise_floor = 0.001; // Approximate noise floor
        metrics_.snr_db = 20.0 * log10(rms / noise_floor);
    }
}

void EnhancedStreamProcessor::apply_normalization(vector<int16_t>& samples) {
    if (samples.empty()) return;
    
    double current_rms = calculate_rms(samples);
    
    // Calculate target gain based on EBU R128 standard
    double target_rms = pow(10.0, config_.target_level_db / 20.0);
    
    if (current_rms > 0.001) {
        target_gain_ = target_rms / current_rms;
        
        // Limit gain to prevent clipping and excessive amplification
        target_gain_ = max(0.1, min(target_gain_, 4.0));
    }
    
    // Smooth gain transitions
    smooth_gain_transition();
    
    // Apply gain to samples
    for (auto& sample : samples) {
        double processed = sample * current_gain_;
        sample = static_cast<int16_t>(max(-32768.0, min(32767.0, processed)));
    }
}

double EnhancedStreamProcessor::calculate_rms(const vector<int16_t>& samples) {
    if (samples.empty()) return 0.0;
    
    double sum_squares = 0.0;
    for (int16_t sample : samples) {
        double normalized = sample / 32768.0;
        sum_squares += normalized * normalized;
    }
    
    return sqrt(sum_squares / samples.size());
}

double EnhancedStreamProcessor::calculate_peak(const vector<int16_t>& samples) {
    if (samples.empty()) return 0.0;
    
    int16_t max_sample = *max_element(samples.begin(), samples.end(), 
                                     [](int16_t a, int16_t b) { return abs(a) < abs(b); });
    
    return abs(max_sample) / 32768.0;
}

bool EnhancedStreamProcessor::detect_silence(const vector<int16_t>& samples) {
    double rms_db = 20.0 * log10(calculate_rms(samples) + 1e-10);
    return rms_db < config_.silence_threshold_db;
}

void EnhancedStreamProcessor::smooth_gain_transition() {
    // Exponential smoothing for gain changes
    current_gain_ += (target_gain_ - current_gain_) * gain_smoothing_;
}

StreamQualityMetrics EnhancedStreamProcessor::get_quality_metrics() const {
    lock_guard<mutex> lock(metrics_mutex_);
    return metrics_;
}

string EnhancedStreamProcessor::get_current_url() const {
    lock_guard<mutex> lock(metrics_mutex_);

    const int index = current_fallback_index_;
    if (index < 0) {
        return config_.primary_url;
    }
    
    if (index < static_cast<int>(config_.fallback_urls.size())) {
        return config_.fallback_urls[index];
    }
    
    return "";
}

bool EnhancedStreamProcessor::update_config(const StreamConfig& config) {
    if (config.primary_url.empty() or config.reconnect_delay_ms < 0 or
            config.max_reconnects < 0 or config.buffer_ms < 0 or
            config.silence_timeout_s < 0 or config.connection_timeout_ms < 0) {
        return false;
    }

    lock_guard<mutex> lock(metrics_mutex_);
    config_ = config;

    // A fallback index that no longer exists would select no URL
    if (current_fallback_index_ >= static_cast<int>(config_.fallback_urls.size())) {
        current_fallback_index_ = -1;
    }

    // Normalisation changes take effect from unity gain
    if (!config_.enable_normalization) {
        current_gain_ = 1.0;
        target_gain_ = 1.0;
    }
    return true;
}

StreamConfig EnhancedStreamProcessor::get_config() const {
    lock_guard<mutex> lock(metrics_mutex_);
    return config_;
}

void EnhancedStreamProcessor::reset_metrics() {
    lock_guard<mutex> lock(metrics_mutex_);

    metrics_ = StreamQualityMetrics();
    metrics_.start_time = steady_clock::now();
    metrics_.last_audio = metrics_.start_time;
    total_samples_ = 0;
    rms_history_.clear();
}

void EnhancedStreamProcessor::cycle_fallback() {
    lock_guard<mutex> lock(metrics_mutex_);

    if (config_.fallback_urls.empty()) {
        return;
    }

    int next = current_fallback_index_ + 1;
    if (next >= static_cast<int>(config_.fallback_urls.size())) {
        next = -1; // back to the primary URL
    }
    current_fallback_index_ = next;
}

bool EnhancedStreamProcessor::force_reconnect() {
    if (!running_) {
        return false;
    }

    {
        lock_guard<mutex> lock(metrics_mutex_);
        metrics_.reconnect_count++;
    }
    connected_ = false;
    reconnect_cv_.notify_all();
    return true;
}

string EnhancedStreamProcessor::get_current_title() const {
    return vlc_input_ ? vlc_input_->get_current_title() : "";
}

string EnhancedStreamProcessor::get_current_artist() const {
    return vlc_input_ ? vlc_input_->get_current_artist() : "";
}

string EnhancedStreamProcessor::get_stream_info() const {
    const auto stats = get_statistics();

    ostringstream oss;
    oss << "url=" << get_current_url()
        << " connected=" << (connected_ ? "yes" : "no")
        << " samples=" << stats.total_samples_processed
        << " reconnects=" << stats.total_reconnects
        << " underruns=" << stats.total_buffer_underruns
        << " bitrate_kbps=" << stats.average_bitrate_kbps;
    return oss.str();
}

EnhancedStreamProcessor::StreamStats EnhancedStreamProcessor::get_statistics() const {
    lock_guard<mutex> lock(metrics_mutex_);

    StreamStats stats;
    stats.total_samples_processed = total_samples_;
    stats.total_reconnects = metrics_.reconnect_count;
    stats.total_buffer_underruns = metrics_.underrun_count;
    stats.uptime_start = metrics_.start_time;

    // Average PCM input rate: 16 bit samples over the time since start
    const double elapsed_s = duration<double>(steady_clock::now() - metrics_.start_time).count();
    if (total_samples_ > 0 and elapsed_s > 0.0) {
        stats.average_bitrate_kbps = (static_cast<double>(total_samples_) * 16.0 / elapsed_s) / 1000.0;
    }

    // The configured network cache is the nominal latency while a stream is connected
    stats.current_latency_ms = connected_ ? static_cast<double>(config_.buffer_ms) : 0.0;
    return stats;
}

bool EnhancedStreamProcessor::is_healthy() const {
    auto issues = get_health_issues();
    return issues.empty();
}

vector<string> EnhancedStreamProcessor::get_health_issues() const {
    vector<string> issues;
    
    if (!connected_) {
        issues.push_back("Stream disconnected");
    }
    
    lock_guard<mutex> lock(metrics_mutex_);

    auto now = steady_clock::now();
    auto silence_duration = duration_cast<seconds>(now - metrics_.last_audio).count();
    
    if (silence_duration > config_.silence_timeout_s / 2) {
        issues.push_back("Prolonged silence detected");
    }
    
    if (metrics_.underrun_count > 10) {
        issues.push_back("Frequent buffer underruns");
    }
    
    if (metrics_.volume_rms < 0.001) {
        issues.push_back("Very low audio level");
    }
    
    return issues;
}

// Stream utility functions
namespace StreamUtils {

bool validate_stream_url(const string& url) {
    auto parsed = StreamURLParser::parse(url);
    return parsed.is_valid && 
           StreamURLParser::is_supported_protocol(parsed.protocol);
}

vector<string> detect_stream_format(const string& url) {
    // Look at the URL path only (not the query string or fragment)
    string path = url;
    const size_t scheme_end = path.find("://");
    if (scheme_end != string::npos) {
        path = path.substr(scheme_end + 3);
        const size_t path_start = path.find('/');
        path = (path_start == string::npos) ? string() : path.substr(path_start);
    }
    path = path.substr(0, path.find_first_of("?#"));

    const size_t dot = path.rfind('.');
    if (dot == string::npos or path.find('/', dot) != string::npos) {
        return {};
    }

    string ext = path.substr(dot + 1);
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const map<string, string> formats = {
        {"mp3", "mp3"}, {"mp2", "mp2"},
        {"aac", "aac"}, {"aacp", "aac"}, {"m4a", "aac"},
        {"ogg", "ogg"}, {"oga", "ogg"}, {"opus", "opus"},
        {"flac", "flac"}, {"wav", "wav"},
        {"m3u8", "hls"}, {"m3u", "playlist"}, {"pls", "playlist"},
    };

    const auto it = formats.find(ext);
    if (it == formats.end()) {
        return {};
    }
    return {it->second};
}

bool test_stream_connectivity(const string& url, int timeout_ms) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && (response_code == 200 || response_code == 206));
}

} // namespace StreamUtils

// URL Parser implementation
StreamURLParser::ParsedURL StreamURLParser::parse(const string& url) {
    ParsedURL result;
    
    regex url_regex(R"(^(https?|icecast|shoutcast)://(?:([^:@]+)(?::([^@]+))?@)?([^:/]+)(?::(\d+))?(/[^?]*)?(?:\?(.*))?$)",
                   regex::icase);
    
    smatch matches;
    if (regex_match(url, matches, url_regex)) {
        result.protocol = matches[1].str();
        result.username = matches[2].str();
        result.password = matches[3].str();
        result.hostname = matches[4].str();
        
        if (matches[5].matched) {
            result.port = stoi(matches[5].str());
        }
        else {
            result.port = (result.protocol == "https") ? 443 : 80;
        }
        
        result.path = matches[6].matched ? matches[6].str() : "/";
        result.query = matches[7].str();
        result.is_valid = true;
    }
    
    return result;
}

bool StreamURLParser::is_supported_protocol(const string& protocol) {
    vector<string> supported = {"http", "https", "icecast", "shoutcast"};
    return find(supported.begin(), supported.end(), protocol) != supported.end();
}

string StreamURLParser::sanitize_url(const string& url) {
    string sanitized = url;
    
    // Remove any potential script injections
    regex script_regex(R"(<script[^>]*>.*?</script>)", regex::icase);
    sanitized = regex_replace(sanitized, script_regex, "");
    
    // Ensure proper URL encoding
    // This is a simplified version - in production, use a proper URL encoding library
    
    return sanitized;
}

} // namespace StreamDAB