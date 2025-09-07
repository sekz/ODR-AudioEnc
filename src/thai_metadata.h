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
#include <map>
#include <chrono>
#include <memory>
#include <cstdint>

namespace StreamDAB {

/*! \file thai_metadata.h
 *  \brief Thai language support for DAB+ broadcasting with ETSI TS 101 756
 *         Thai profile (0x0E) character set conversion and metadata processing.
 */

// Thai character encoding constants
constexpr uint8_t DAB_THAI_CHARSET = 0x0E;  // ETSI TS 101 756 Thai profile
constexpr size_t MAX_DLS_LENGTH_THAI = 128; // DAB DLS maximum length
constexpr size_t MAX_LABEL_LENGTH = 16;     // Service label maximum length

// Thai language detection patterns
struct ThaiLanguagePattern {
    std::string pattern;
    double confidence;
    std::string description;
};

// Thai metadata structure
struct ThaiMetadata {
    std::string title_utf8;
    std::string artist_utf8;
    std::string album_utf8;
    std::string station_utf8;
    
    std::string title_dab;      // DAB+ encoded
    std::string artist_dab;     // DAB+ encoded
    std::string album_dab;      // DAB+ encoded  
    std::string station_dab;    // DAB+ encoded
    
    bool is_thai_content = false;
    double thai_confidence = 0.0;
    std::chrono::system_clock::time_point timestamp;
    
    // Buddhist calendar support
    struct BuddhistDate {
        int year = 0;        // Buddhist Era (BE)
        int month = 0;
        int day = 0;
        std::string thai_month_name;
        bool is_valid = false;
    };
    
    BuddhistDate buddhist_date;
};

// DLS (Dynamic Label Segment) processor for Thai content
class ThaiDLSProcessor {
private:
    size_t max_segment_length_;
    std::vector<uint8_t> current_segment_;
    std::string pending_text_;
    bool scrolling_enabled_;
    int scroll_speed_ms_;
    
    // Character width calculation for Thai text
    size_t calculate_display_width(const std::string& thai_text);
    std::vector<std::string> split_into_segments(const std::string& text, size_t max_width);

public:
    ThaiDLSProcessor(size_t max_length = MAX_DLS_LENGTH_THAI, bool scrolling = true);
    
    // Process Thai text into DLS segments
    std::vector<uint8_t> process_thai_text(const std::string& utf8_text);
    
    // Get current DLS segment
    std::vector<uint8_t> get_current_segment() const { return current_segment_; }
    
    // Update scrolling parameters
    void set_scrolling(bool enabled, int speed_ms = 200);
    
    // Validate Thai DLS content
    bool validate_dls_content(const std::vector<uint8_t>& dls_data);
};

// Character set conversion utilities
class ThaiCharsetConverter {
public:
    // Core conversion functions
    static std::string utf8_to_dab_thai(const std::string& utf8_input);
    static std::string dab_thai_to_utf8(const std::vector<uint8_t>& dab_input);
    
    // Character validation
    static bool is_valid_thai_utf8(const std::string& text);
    static bool is_valid_dab_thai(const std::vector<uint8_t>& data);
    
    // Character normalization
    static std::string normalize_thai_text(const std::string& input);
    static std::string remove_invalid_characters(const std::string& input);
    
    // Length calculation considering Thai character width
    static size_t calculate_thai_display_length(const std::string& utf8_text);
    static std::string truncate_thai_text(const std::string& utf8_text, size_t max_length);

private:
    static std::map<uint32_t, uint8_t> create_utf8_to_dab_thai_map();
    static std::map<uint8_t, uint32_t> create_dab_thai_to_utf8_map();
    
    static const std::map<uint32_t, uint8_t> utf8_to_dab_map_;
    static const std::map<uint8_t, uint32_t> dab_to_utf8_map_;
};

// Thai language detection and analysis
class ThaiLanguageDetector {
private:
    std::vector<ThaiLanguagePattern> patterns_;
    
    void initialize_patterns();
    double calculate_thai_score(const std::string& text);
    bool contains_thai_characters(const std::string& text);

public:
    ThaiLanguageDetector();
    
    // Language detection
    bool is_thai_text(const std::string& text, double threshold = 0.7);
    double get_thai_confidence(const std::string& text);
    
    // Content analysis
    std::vector<std::string> extract_thai_words(const std::string& text);
    bool is_mixed_language(const std::string& text);
    
    // Statistics
    struct LanguageStats {
        size_t thai_char_count = 0;
        size_t english_char_count = 0;
        size_t total_char_count = 0;
        double thai_percentage = 0.0;
        bool has_thai_vowels = false;
        bool has_thai_consonants = false;
    };
    
    LanguageStats analyze_language_composition(const std::string& text);
};

// Buddhist calendar conversion utilities
class BuddhistCalendar {
public:
    // Date conversion
    static ThaiMetadata::BuddhistDate gregorian_to_buddhist(int year, int month, int day);
    static std::chrono::system_clock::time_point buddhist_to_gregorian(
        int be_year, int month, int day);
    
    // Thai month names
    static std::string get_thai_month_name(int month);
    static std::string get_thai_day_name(int day_of_week);
    
    // Date formatting
    static std::string format_buddhist_date(const ThaiMetadata::BuddhistDate& date);
    static std::string format_current_buddhist_date();
    
    // Validation
    static bool is_valid_buddhist_date(int be_year, int month, int day);
    
    // Special dates and holidays
    static bool is_buddhist_holiday(int month, int day);
    static std::vector<std::string> get_buddhist_holidays_for_month(int month);

private:
    static const std::vector<std::string> thai_month_names_;
    static const std::vector<std::string> thai_day_names_;
    static const std::map<std::pair<int, int>, std::string> buddhist_holidays_;
};

// Metadata extraction and processing
class ThaiMetadataProcessor {
private:
    ThaiLanguageDetector detector_;
    ThaiDLSProcessor dls_processor_;
    
    // Metadata cleanup
    std::string clean_metadata_field(const std::string& input);
    std::string extract_thai_title_patterns(const std::string& raw_metadata);

public:
    ThaiMetadataProcessor();
    
    // Main processing functions
    ThaiMetadata process_raw_metadata(const std::string& raw_title,
                                     const std::string& raw_artist = "",
                                     const std::string& raw_album = "",
                                     const std::string& raw_station = "");
    
    // Update existing metadata
    void update_metadata(ThaiMetadata& metadata, const std::string& new_data);
    
    // DLS generation
    std::vector<uint8_t> generate_dls_from_metadata(const ThaiMetadata& metadata);
    
    // Validation
    bool validate_metadata(const ThaiMetadata& metadata);
    
    // Format for different uses
    std::string format_for_display(const ThaiMetadata& metadata);
    std::string format_for_logging(const ThaiMetadata& metadata);
    
    // Statistics and monitoring
    struct ProcessingStats {
        size_t total_metadata_processed = 0;
        size_t thai_content_detected = 0;
        size_t conversion_errors = 0;
        std::chrono::steady_clock::time_point last_processed;
        double average_thai_confidence = 0.0;
    };
    
    ProcessingStats get_processing_stats() const { return stats_; }
    void reset_stats();

private:
    ProcessingStats stats_;
};

// Utility functions
namespace ThaiUtils {
    // Text processing utilities
    std::string normalize_whitespace(const std::string& input);
    std::string remove_control_characters(const std::string& input);
    std::vector<std::string> split_thai_sentences(const std::string& text);
    
    // Character analysis
    bool is_thai_vowel(uint32_t codepoint);
    bool is_thai_consonant(uint32_t codepoint);
    bool is_thai_tone_mark(uint32_t codepoint);
    bool is_thai_number(uint32_t codepoint);
    
    // UTF-8 utilities
    std::vector<uint32_t> utf8_to_codepoints(const std::string& utf8_string);
    std::string codepoints_to_utf8(const std::vector<uint32_t>& codepoints);
    
    // Validation
    bool is_valid_utf8_sequence(const std::string& input);
    size_t count_thai_characters(const std::string& utf8_text);
    
    // Display utilities
    std::string add_thai_line_breaks(const std::string& text, size_t max_width);
    std::string pad_thai_text(const std::string& text, size_t target_width);
}

// Error handling
enum class ThaiProcessingError {
    None,
    InvalidUTF8,
    ConversionFailed,
    TextTooLong,
    InvalidCharacter,
    EncodingError,
    ValidationFailed
};

class ThaiProcessingException : public std::exception {
private:
    ThaiProcessingError error_code_;
    std::string message_;
    
public:
    ThaiProcessingException(ThaiProcessingError code, const std::string& message)
        : error_code_(code), message_(message) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    ThaiProcessingError get_error_code() const { return error_code_; }
};

} // namespace StreamDAB