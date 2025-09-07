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

#include "thai_metadata.h"
#include <algorithm>
#include <regex>
#include <codecvt>
#include <locale>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

namespace StreamDAB {

// Static member initialization
const vector<string> BuddhistCalendar::thai_month_names_ = {
    "", "มกราคม", "กุมภาพันธ์", "มีนาคม", "เมษายน", "พฤษภาคม", "มิถุนายน",
    "กรกฎาคม", "สิงหาคม", "กันยายน", "ตุลาคม", "พฤศจิกายน", "ธันวาคม"
};

const vector<string> BuddhistCalendar::thai_day_names_ = {
    "อาทิตย์", "จันทร์", "อังคาร", "พุธ", "พฤหัสบดี", "ศุกร์", "เสาร์"
};

// Thai Unicode ranges
constexpr uint32_t THAI_BLOCK_START = 0x0E00;
constexpr uint32_t THAI_BLOCK_END = 0x0E7F;

// ThaiDLSProcessor implementation
ThaiDLSProcessor::ThaiDLSProcessor(size_t max_length, bool scrolling) 
    : max_segment_length_(max_length), scrolling_enabled_(scrolling), scroll_speed_ms_(200) {
    current_segment_.reserve(max_segment_length_);
}

vector<uint8_t> ThaiDLSProcessor::process_thai_text(const string& utf8_text) {
    if (utf8_text.empty()) {
        return vector<uint8_t>();
    }
    
    // Convert UTF-8 to DAB Thai charset
    string dab_text = ThaiCharsetConverter::utf8_to_dab_thai(utf8_text);
    
    // Calculate display width
    size_t display_width = calculate_display_width(utf8_text);
    
    // If text fits in one segment
    if (display_width <= max_segment_length_) {
        vector<uint8_t> result(dab_text.begin(), dab_text.end());
        
        // Add charset indicator
        result.insert(result.begin(), DAB_THAI_CHARSET);
        
        current_segment_ = result;
        return result;
    }
    
    // Handle long text with scrolling or segmentation
    if (scrolling_enabled_) {
        // For now, just truncate. Full scrolling would require state management
        string truncated = ThaiCharsetConverter::truncate_thai_text(utf8_text, max_segment_length_ - 1);
        dab_text = ThaiCharsetConverter::utf8_to_dab_thai(truncated);
        
        vector<uint8_t> result(dab_text.begin(), dab_text.end());
        result.insert(result.begin(), DAB_THAI_CHARSET);
        
        current_segment_ = result;
        return result;
    }
    
    return current_segment_;
}

size_t ThaiDLSProcessor::calculate_display_width(const string& thai_text) {
    return ThaiCharsetConverter::calculate_thai_display_length(thai_text);
}

// ThaiCharsetConverter implementation
map<uint32_t, uint8_t> ThaiCharsetConverter::create_utf8_to_dab_thai_map() {
    map<uint32_t, uint8_t> mapping;
    
    // Basic Thai consonants (simplified mapping for demonstration)
    // In production, this should include the complete ETSI TS 101 756 mapping
    mapping[0x0E01] = 0x81; // ก
    mapping[0x0E02] = 0x82; // ข
    mapping[0x0E03] = 0x83; // ฃ
    mapping[0x0E04] = 0x84; // ค
    mapping[0x0E05] = 0x85; // ฅ
    mapping[0x0E06] = 0x86; // ฆ
    mapping[0x0E07] = 0x87; // ง
    mapping[0x0E08] = 0x88; // จ
    mapping[0x0E09] = 0x89; // ฉ
    mapping[0x0E0A] = 0x8A; // ช
    mapping[0x0E0B] = 0x8B; // ซ
    mapping[0x0E0C] = 0x8C; // ฌ
    mapping[0x0E0D] = 0x8D; // ญ
    mapping[0x0E0E] = 0x8E; // ฎ
    mapping[0x0E0F] = 0x8F; // ฏ
    
    // Thai vowels
    mapping[0x0E30] = 0xB0; // ะ
    mapping[0x0E31] = 0xB1; // ั
    mapping[0x0E32] = 0xB2; // า
    mapping[0x0E33] = 0xB3; // ำ
    mapping[0x0E34] = 0xB4; // ิ
    mapping[0x0E35] = 0xB5; // ี
    mapping[0x0E36] = 0xB6; // ึ
    mapping[0x0E37] = 0xB7; // ื
    mapping[0x0E38] = 0xB8; // ุ
    mapping[0x0E39] = 0xB9; // ู
    mapping[0x0E3A] = 0xBA; // ฺ
    
    // Thai tone marks
    mapping[0x0E48] = 0xC8; // ่
    mapping[0x0E49] = 0xC9; // ้
    mapping[0x0E4A] = 0xCA; // ๊
    mapping[0x0E4B] = 0xCB; // ๋
    mapping[0x0E4C] = 0xCC; // ์
    
    // Thai numbers
    mapping[0x0E50] = 0xD0; // ๐
    mapping[0x0E51] = 0xD1; // ๑
    mapping[0x0E52] = 0xD2; // ๒
    mapping[0x0E53] = 0xD3; // ๓
    mapping[0x0E54] = 0xD4; // ๔
    mapping[0x0E55] = 0xD5; // ๕
    mapping[0x0E56] = 0xD6; // ๖
    mapping[0x0E57] = 0xD7; // ๗
    mapping[0x0E58] = 0xD8; // ๘
    mapping[0x0E59] = 0xD9; // ๙
    
    // Add ASCII characters (0x20-0x7F remain the same)
    for (uint32_t i = 0x20; i <= 0x7F; ++i) {
        mapping[i] = static_cast<uint8_t>(i);
    }
    
    return mapping;
}

const map<uint32_t, uint8_t> ThaiCharsetConverter::utf8_to_dab_map_ = 
    ThaiCharsetConverter::create_utf8_to_dab_thai_map();

string ThaiCharsetConverter::utf8_to_dab_thai(const string& utf8_input) {
    if (!is_valid_thai_utf8(utf8_input)) {
        throw ThaiProcessingException(ThaiProcessingError::InvalidUTF8, 
                                    "Invalid UTF-8 sequence in input");
    }
    
    auto codepoints = ThaiUtils::utf8_to_codepoints(utf8_input);
    string result;
    
    for (uint32_t cp : codepoints) {
        auto it = utf8_to_dab_map_.find(cp);
        if (it != utf8_to_dab_map_.end()) {
            result.push_back(static_cast<char>(it->second));
        }
        else {
            // Handle unmapped characters - could log warning or use replacement
            if (cp >= 0x20 && cp <= 0x7F) {
                result.push_back(static_cast<char>(cp)); // ASCII passthrough
            }
            else {
                result.push_back('?'); // Replacement character
            }
        }
    }
    
    return result;
}

bool ThaiCharsetConverter::is_valid_thai_utf8(const string& text) {
    return ThaiUtils::is_valid_utf8_sequence(text);
}

string ThaiCharsetConverter::normalize_thai_text(const string& input) {
    string result = input;
    
    // Remove control characters
    result = ThaiUtils::remove_control_characters(result);
    
    // Normalize whitespace
    result = ThaiUtils::normalize_whitespace(result);
    
    // Additional Thai-specific normalization could be added here
    
    return result;
}

size_t ThaiCharsetConverter::calculate_thai_display_length(const string& utf8_text) {
    auto codepoints = ThaiUtils::utf8_to_codepoints(utf8_text);
    size_t length = 0;
    
    for (uint32_t cp : codepoints) {
        // Thai combining characters (vowels, tone marks) don't add to display width
        if (ThaiUtils::is_thai_vowel(cp) && cp >= 0x0E31 && cp <= 0x0E3A) {
            // Non-spacing vowels don't add width
            continue;
        }
        if (ThaiUtils::is_thai_tone_mark(cp)) {
            // Tone marks don't add width
            continue;
        }
        if (cp == 0x0E4C) { // Thai character thanthakhat (cancellation mark)
            continue;
        }
        
        length++;
    }
    
    return length;
}

string ThaiCharsetConverter::truncate_thai_text(const string& utf8_text, size_t max_length) {
    if (calculate_thai_display_length(utf8_text) <= max_length) {
        return utf8_text;
    }
    
    auto codepoints = ThaiUtils::utf8_to_codepoints(utf8_text);
    vector<uint32_t> result_codepoints;
    size_t current_length = 0;
    
    for (uint32_t cp : codepoints) {
        // Check if adding this character would exceed the limit
        bool adds_width = true;
        if (ThaiUtils::is_thai_vowel(cp) && cp >= 0x0E31 && cp <= 0x0E3A) {
            adds_width = false;
        }
        if (ThaiUtils::is_thai_tone_mark(cp) || cp == 0x0E4C) {
            adds_width = false;
        }
        
        if (adds_width && current_length >= max_length) {
            break;
        }
        
        result_codepoints.push_back(cp);
        if (adds_width) {
            current_length++;
        }
    }
    
    return ThaiUtils::codepoints_to_utf8(result_codepoints);
}

// ThaiLanguageDetector implementation
ThaiLanguageDetector::ThaiLanguageDetector() {
    initialize_patterns();
}

void ThaiLanguageDetector::initialize_patterns() {
    patterns_ = {
        {u8"[ก-๏]", 0.9, "Thai Unicode block characters"},
        {u8"[๐-๙]", 0.7, "Thai digits"},
        {u8"[่-๋]", 0.8, "Thai tone marks"},
        {u8"แ[ก-ฮ]", 0.85, "Thai syllable patterns"},
        {u8"เ[ก-ฮ]", 0.85, "Thai syllable patterns"},
        {u8"โ[ก-ฮ]", 0.85, "Thai syllable patterns"},
    };
}

bool ThaiLanguageDetector::is_thai_text(const string& text, double threshold) {
    return get_thai_confidence(text) >= threshold;
}

double ThaiLanguageDetector::get_thai_confidence(const string& text) {
    if (text.empty()) {
        return 0.0;
    }
    
    auto stats = analyze_language_composition(text);
    return stats.thai_percentage;
}

ThaiLanguageDetector::LanguageStats 
ThaiLanguageDetector::analyze_language_composition(const string& text) {
    LanguageStats stats;
    
    auto codepoints = ThaiUtils::utf8_to_codepoints(text);
    
    for (uint32_t cp : codepoints) {
        stats.total_char_count++;
        
        if (cp >= THAI_BLOCK_START && cp <= THAI_BLOCK_END) {
            stats.thai_char_count++;
            
            if (ThaiUtils::is_thai_vowel(cp)) {
                stats.has_thai_vowels = true;
            }
            if (ThaiUtils::is_thai_consonant(cp)) {
                stats.has_thai_consonants = true;
            }
        }
        else if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
            stats.english_char_count++;
        }
    }
    
    if (stats.total_char_count > 0) {
        stats.thai_percentage = static_cast<double>(stats.thai_char_count) / 
                               stats.total_char_count;
    }
    
    return stats;
}

// BuddhistCalendar implementation
ThaiMetadata::BuddhistDate BuddhistCalendar::gregorian_to_buddhist(int year, int month, int day) {
    ThaiMetadata::BuddhistDate result;
    
    if (is_valid_buddhist_date(year + 543, month, day)) {
        result.year = year + 543; // Add 543 years to convert to Buddhist Era
        result.month = month;
        result.day = day;
        result.thai_month_name = get_thai_month_name(month);
        result.is_valid = true;
    }
    
    return result;
}

string BuddhistCalendar::get_thai_month_name(int month) {
    if (month >= 1 && month <= 12) {
        return thai_month_names_[month];
    }
    return "";
}

string BuddhistCalendar::format_current_buddhist_date() {
    auto now = chrono::system_clock::now();
    time_t now_t = chrono::system_clock::to_time_t(now);
    tm* now_tm = localtime(&now_t);
    
    auto buddhist_date = gregorian_to_buddhist(now_tm->tm_year + 1900, 
                                               now_tm->tm_mon + 1, 
                                               now_tm->tm_mday);
    
    return format_buddhist_date(buddhist_date);
}

string BuddhistCalendar::format_buddhist_date(const ThaiMetadata::BuddhistDate& date) {
    if (!date.is_valid) {
        return "";
    }
    
    stringstream ss;
    ss << date.day << " " << date.thai_month_name << " พ.ศ. " << date.year;
    return ss.str();
}

bool BuddhistCalendar::is_valid_buddhist_date(int be_year, int month, int day) {
    return (be_year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31);
}

// ThaiMetadataProcessor implementation
ThaiMetadataProcessor::ThaiMetadataProcessor() : dls_processor_(MAX_DLS_LENGTH_THAI, true) {
    stats_.last_processed = chrono::steady_clock::now();
}

ThaiMetadata ThaiMetadataProcessor::process_raw_metadata(const string& raw_title,
                                                        const string& raw_artist,
                                                        const string& raw_album,
                                                        const string& raw_station) {
    ThaiMetadata result;
    result.timestamp = chrono::system_clock::now();
    
    // Clean and process each field
    result.title_utf8 = clean_metadata_field(raw_title);
    result.artist_utf8 = clean_metadata_field(raw_artist);
    result.album_utf8 = clean_metadata_field(raw_album);
    result.station_utf8 = clean_metadata_field(raw_station);
    
    // Detect Thai content
    string combined_text = result.title_utf8 + " " + result.artist_utf8;
    result.is_thai_content = detector_.is_thai_text(combined_text);
    result.thai_confidence = detector_.get_thai_confidence(combined_text);
    
    // Convert to DAB+ encoding if Thai content detected
    if (result.is_thai_content) {
        try {
            result.title_dab = ThaiCharsetConverter::utf8_to_dab_thai(result.title_utf8);
            result.artist_dab = ThaiCharsetConverter::utf8_to_dab_thai(result.artist_utf8);
            result.album_dab = ThaiCharsetConverter::utf8_to_dab_thai(result.album_utf8);
            result.station_dab = ThaiCharsetConverter::utf8_to_dab_thai(result.station_utf8);
        }
        catch (const ThaiProcessingException& e) {
            // Log error but continue with UTF-8 versions
            fprintf(stderr, "Thai charset conversion error: %s\n", e.what());
            stats_.conversion_errors++;
        }
    }
    
    // Add Buddhist calendar date
    result.buddhist_date = BuddhistCalendar::gregorian_to_buddhist(
        chrono::system_clock::to_time_t(result.timestamp));
    
    // Update statistics
    stats_.total_metadata_processed++;
    if (result.is_thai_content) {
        stats_.thai_content_detected++;
        stats_.average_thai_confidence = 
            (stats_.average_thai_confidence * (stats_.thai_content_detected - 1) + 
             result.thai_confidence) / stats_.thai_content_detected;
    }
    stats_.last_processed = chrono::steady_clock::now();
    
    return result;
}

string ThaiMetadataProcessor::clean_metadata_field(const string& input) {
    string result = input;
    
    // Remove control characters
    result = ThaiUtils::remove_control_characters(result);
    
    // Normalize whitespace
    result = ThaiUtils::normalize_whitespace(result);
    
    // Normalize Thai characters
    result = ThaiCharsetConverter::normalize_thai_text(result);
    
    return result;
}

vector<uint8_t> ThaiMetadataProcessor::generate_dls_from_metadata(const ThaiMetadata& metadata) {
    string dls_text;
    
    if (!metadata.title_utf8.empty()) {
        dls_text = metadata.title_utf8;
        
        if (!metadata.artist_utf8.empty()) {
            dls_text += " - " + metadata.artist_utf8;
        }
    }
    else if (!metadata.artist_utf8.empty()) {
        dls_text = metadata.artist_utf8;
    }
    
    if (dls_text.empty()) {
        dls_text = metadata.station_utf8;
    }
    
    return dls_processor_.process_thai_text(dls_text);
}

// Thai utility functions
namespace ThaiUtils {

vector<uint32_t> utf8_to_codepoints(const string& utf8_string) {
    vector<uint32_t> codepoints;
    
    try {
        wstring_convert<codecvt_utf8<char32_t>, char32_t> converter;
        u32string utf32_string = converter.from_bytes(utf8_string);
        
        codepoints.reserve(utf32_string.length());
        for (char32_t cp : utf32_string) {
            codepoints.push_back(static_cast<uint32_t>(cp));
        }
    }
    catch (const exception& e) {
        // Handle conversion error
        fprintf(stderr, "UTF-8 to codepoint conversion error: %s\n", e.what());
    }
    
    return codepoints;
}

string codepoints_to_utf8(const vector<uint32_t>& codepoints) {
    try {
        u32string utf32_string;
        utf32_string.reserve(codepoints.size());
        
        for (uint32_t cp : codepoints) {
            utf32_string.push_back(static_cast<char32_t>(cp));
        }
        
        wstring_convert<codecvt_utf8<char32_t>, char32_t> converter;
        return converter.to_bytes(utf32_string);
    }
    catch (const exception& e) {
        fprintf(stderr, "Codepoint to UTF-8 conversion error: %s\n", e.what());
        return "";
    }
}

bool is_thai_consonant(uint32_t codepoint) {
    return (codepoint >= 0x0E01 && codepoint <= 0x0E2E);
}

bool is_thai_vowel(uint32_t codepoint) {
    return (codepoint >= 0x0E30 && codepoint <= 0x0E3A) ||
           (codepoint >= 0x0E40 && codepoint <= 0x0E44);
}

bool is_thai_tone_mark(uint32_t codepoint) {
    return (codepoint >= 0x0E48 && codepoint <= 0x0E4B);
}

bool is_thai_number(uint32_t codepoint) {
    return (codepoint >= 0x0E50 && codepoint <= 0x0E59);
}

string normalize_whitespace(const string& input) {
    string result;
    bool in_whitespace = false;
    
    for (char c : input) {
        if (isspace(c)) {
            if (!in_whitespace) {
                result.push_back(' ');
                in_whitespace = true;
            }
        }
        else {
            result.push_back(c);
            in_whitespace = false;
        }
    }
    
    // Trim leading/trailing whitespace
    if (!result.empty() && result.front() == ' ') {
        result.erase(0, 1);
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

string remove_control_characters(const string& input) {
    string result;
    result.reserve(input.length());
    
    for (char c : input) {
        // Remove control characters except tab, newline, carriage return
        if (c >= 32 || c == '\t' || c == '\n' || c == '\r') {
            result.push_back(c);
        }
    }
    
    return result;
}

bool is_valid_utf8_sequence(const string& input) {
    try {
        wstring_convert<codecvt_utf8<char32_t>, char32_t> converter;
        converter.from_bytes(input);
        return true;
    }
    catch (...) {
        return false;
    }
}

size_t count_thai_characters(const string& utf8_text) {
    auto codepoints = utf8_to_codepoints(utf8_text);
    size_t count = 0;
    
    for (uint32_t cp : codepoints) {
        if (cp >= THAI_BLOCK_START && cp <= THAI_BLOCK_END) {
            count++;
        }
    }
    
    return count;
}

} // namespace ThaiUtils

} // namespace StreamDAB