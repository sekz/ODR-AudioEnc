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
#include "thai_metadata.h"

using namespace StreamDAB;
using namespace std;

class ThaiMetadataTest : public ::testing::Test {
protected:
    void SetUp() override {
        processor_ = make_unique<ThaiMetadataProcessor>();
        detector_ = make_unique<ThaiLanguageDetector>();
        dls_processor_ = make_unique<ThaiDLSProcessor>();
    }
    
    unique_ptr<ThaiMetadataProcessor> processor_;
    unique_ptr<ThaiLanguageDetector> detector_;
    unique_ptr<ThaiDLSProcessor> dls_processor_;
    
    // Test data constants
    const string thai_text_ = "สวัสดีครับ";
    const string english_text_ = "Hello World";
    const string mixed_text_ = "Hello สวัสดี World";
    const string thai_song_title_ = "เพลงไทยสมัยใหม่";
    const string thai_artist_name_ = "นักร้องไทย";
};

// ThaiCharsetConverter Tests
TEST_F(ThaiMetadataTest, UTF8ToDabThaiConversion) {
    EXPECT_NO_THROW({
        string converted = ThaiCharsetConverter::utf8_to_dab_thai(thai_text_);
        EXPECT_FALSE(converted.empty());
    });
}

TEST_F(ThaiMetadataTest, ValidateThaiUTF8) {
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8(thai_text_));
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8(english_text_));
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8(mixed_text_));
    
    // Test invalid UTF-8
    string invalid_utf8 = "\xFF\xFE\xInvalid";
    EXPECT_FALSE(ThaiCharsetConverter::is_valid_thai_utf8(invalid_utf8));
}

TEST_F(ThaiMetadataTest, ThaiTextNormalization) {
    string input = "  สวัสดี  \t\n  ครับ  ";
    string normalized = ThaiCharsetConverter::normalize_thai_text(input);
    
    EXPECT_EQ(normalized, "สวัสดี ครับ");
}

TEST_F(ThaiMetadataTest, CalculateThaiDisplayLength) {
    // Thai text with combining characters
    string thai_with_vowels = "กำ"; // ก + ำ (combining vowel)
    
    size_t length = ThaiCharsetConverter::calculate_thai_display_length(thai_with_vowels);
    EXPECT_EQ(length, 1); // Should count as 1 display character
    
    length = ThaiCharsetConverter::calculate_thai_display_length(thai_text_);
    EXPECT_GT(length, 0);
}

TEST_F(ThaiMetadataTest, TruncateThaiText) {
    string long_thai = "สวัสดีครับผมชื่อจอห์นสมิท";
    string truncated = ThaiCharsetConverter::truncate_thai_text(long_thai, 5);
    
    size_t display_length = ThaiCharsetConverter::calculate_thai_display_length(truncated);
    EXPECT_LE(display_length, 5);
}

// ThaiLanguageDetector Tests
TEST_F(ThaiMetadataTest, DetectThaiLanguage) {
    EXPECT_TRUE(detector_->is_thai_text(thai_text_));
    EXPECT_FALSE(detector_->is_thai_text(english_text_));
    
    // Mixed text should be detected based on threshold
    bool is_mixed_thai = detector_->is_thai_text(mixed_text_, 0.3); // Lower threshold
    EXPECT_TRUE(is_mixed_thai);
}

TEST_F(ThaiMetadataTest, ThaiConfidenceScoring) {
    double thai_confidence = detector_->get_thai_confidence(thai_text_);
    EXPECT_GT(thai_confidence, 0.7);
    
    double english_confidence = detector_->get_thai_confidence(english_text_);
    EXPECT_LT(english_confidence, 0.3);
    
    double mixed_confidence = detector_->get_thai_confidence(mixed_text_);
    EXPECT_GT(mixed_confidence, 0.1);
    EXPECT_LT(mixed_confidence, 0.8);
}

TEST_F(ThaiMetadataTest, AnalyzeLanguageComposition) {
    auto stats = detector_->analyze_language_composition(mixed_text_);
    
    EXPECT_GT(stats.thai_char_count, 0);
    EXPECT_GT(stats.english_char_count, 0);
    EXPECT_GT(stats.total_char_count, stats.thai_char_count + stats.english_char_count);
    EXPECT_TRUE(stats.has_thai_consonants || stats.has_thai_vowels);
}

// ThaiDLSProcessor Tests
TEST_F(ThaiMetadataTest, ProcessThaiDLS) {
    vector<uint8_t> dls_data = dls_processor_->process_thai_text(thai_text_);
    
    EXPECT_FALSE(dls_data.empty());
    EXPECT_EQ(dls_data[0], DAB_THAI_CHARSET); // Should start with charset indicator
}

TEST_F(ThaiMetadataTest, DLSLengthConstraints) {
    string long_text = string(200, 'ก'); // Very long Thai text
    vector<uint8_t> dls_data = dls_processor_->process_thai_text(long_text);
    
    EXPECT_LE(dls_data.size(), MAX_DLS_LENGTH_THAI + 1); // +1 for charset indicator
}

TEST_F(ThaiMetadataTest, ValidateDLSContent) {
    vector<uint8_t> dls_data = dls_processor_->process_thai_text(thai_text_);
    EXPECT_TRUE(dls_processor_->validate_dls_content(dls_data));
    
    // Test invalid DLS data
    vector<uint8_t> invalid_data = {0xFF, 0xFE, 0xFD}; // Invalid charset/data
    EXPECT_FALSE(dls_processor_->validate_dls_content(invalid_data));
}

// BuddhistCalendar Tests
TEST_F(ThaiMetadataTest, GregorianToBuddhistConversion) {
    auto buddhist_date = BuddhistCalendar::gregorian_to_buddhist(2024, 9, 7);
    
    EXPECT_TRUE(buddhist_date.is_valid);
    EXPECT_EQ(buddhist_date.year, 2567); // 2024 + 543
    EXPECT_EQ(buddhist_date.month, 9);
    EXPECT_EQ(buddhist_date.day, 7);
    EXPECT_FALSE(buddhist_date.thai_month_name.empty());
}

TEST_F(ThaiMetadataTest, ThaiMonthNames) {
    for (int month = 1; month <= 12; ++month) {
        string month_name = BuddhistCalendar::get_thai_month_name(month);
        EXPECT_FALSE(month_name.empty());
        EXPECT_GT(month_name.length(), 0);
    }
    
    // Invalid month should return empty string
    EXPECT_TRUE(BuddhistCalendar::get_thai_month_name(0).empty());
    EXPECT_TRUE(BuddhistCalendar::get_thai_month_name(13).empty());
}

TEST_F(ThaiMetadataTest, FormatBuddhistDate) {
    auto buddhist_date = BuddhistCalendar::gregorian_to_buddhist(2024, 9, 7);
    string formatted = BuddhistCalendar::format_buddhist_date(buddhist_date);
    
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("2567"), string::npos); // Should contain BE year
    EXPECT_NE(formatted.find("พ.ศ."), string::npos); // Should contain "BE" in Thai
}

TEST_F(ThaiMetadataTest, ValidateBuddhistDate) {
    EXPECT_TRUE(BuddhistCalendar::is_valid_buddhist_date(2567, 9, 7));
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(-1, 9, 7));
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 13, 7));
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 9, 32));
}

// ThaiMetadataProcessor Tests
TEST_F(ThaiMetadataTest, ProcessRawMetadata) {
    ThaiMetadata metadata = processor_->process_raw_metadata(
        thai_song_title_, thai_artist_name_, "", "วิทยุไทย");
    
    EXPECT_EQ(metadata.title_utf8, thai_song_title_);
    EXPECT_EQ(metadata.artist_utf8, thai_artist_name_);
    EXPECT_TRUE(metadata.is_thai_content);
    EXPECT_GT(metadata.thai_confidence, 0.7);
    EXPECT_TRUE(metadata.buddhist_date.is_valid);
    
    // Check DAB conversion happened
    EXPECT_FALSE(metadata.title_dab.empty());
    EXPECT_FALSE(metadata.artist_dab.empty());
}

TEST_F(ThaiMetadataTest, ProcessEnglishMetadata) {
    ThaiMetadata metadata = processor_->process_raw_metadata(
        "English Song", "English Artist", "English Album", "Radio Station");
    
    EXPECT_FALSE(metadata.is_thai_content);
    EXPECT_LT(metadata.thai_confidence, 0.3);
    
    // DAB fields should be empty for non-Thai content
    EXPECT_TRUE(metadata.title_dab.empty() || metadata.title_dab == metadata.title_utf8);
}

TEST_F(ThaiMetadataTest, GenerateDLSFromMetadata) {
    ThaiMetadata metadata = processor_->process_raw_metadata(
        thai_song_title_, thai_artist_name_);
    
    vector<uint8_t> dls_data = processor_->generate_dls_from_metadata(metadata);
    
    EXPECT_FALSE(dls_data.empty());
    EXPECT_EQ(dls_data[0], DAB_THAI_CHARSET); // Thai charset indicator
}

TEST_F(ThaiMetadataTest, ValidateMetadata) {
    ThaiMetadata valid_metadata = processor_->process_raw_metadata(
        thai_song_title_, thai_artist_name_);
    
    EXPECT_TRUE(processor_->validate_metadata(valid_metadata));
    
    // Test invalid metadata
    ThaiMetadata invalid_metadata;
    invalid_metadata.title_utf8 = string(1000, 'ก'); // Too long
    EXPECT_FALSE(processor_->validate_metadata(invalid_metadata));
}

TEST_F(ThaiMetadataTest, ProcessingStatistics) {
    // Process several metadata entries
    for (int i = 0; i < 10; ++i) {
        processor_->process_raw_metadata("Title " + to_string(i), "Artist");
    }
    
    for (int i = 0; i < 5; ++i) {
        processor_->process_raw_metadata(thai_song_title_, thai_artist_name_);
    }
    
    auto stats = processor_->get_processing_stats();
    EXPECT_EQ(stats.total_metadata_processed, 15);
    EXPECT_EQ(stats.thai_content_detected, 5);
    EXPECT_GT(stats.average_thai_confidence, 0.5);
}

// Thai Utility Functions Tests
TEST_F(ThaiMetadataTest, UTF8CodepointConversion) {
    auto codepoints = ThaiUtils::utf8_to_codepoints(thai_text_);
    EXPECT_FALSE(codepoints.empty());
    
    string reconstructed = ThaiUtils::codepoints_to_utf8(codepoints);
    EXPECT_EQ(reconstructed, thai_text_);
}

TEST_F(ThaiMetadataTest, ThaiCharacterClassification) {
    uint32_t thai_consonant = 0x0E01; // ก
    uint32_t thai_vowel = 0x0E32; // า
    uint32_t thai_tone = 0x0E48; // ่
    uint32_t thai_number = 0x0E50; // ๐
    
    EXPECT_TRUE(ThaiUtils::is_thai_consonant(thai_consonant));
    EXPECT_TRUE(ThaiUtils::is_thai_vowel(thai_vowel));
    EXPECT_TRUE(ThaiUtils::is_thai_tone_mark(thai_tone));
    EXPECT_TRUE(ThaiUtils::is_thai_number(thai_number));
    
    // English characters should not be classified as Thai
    EXPECT_FALSE(ThaiUtils::is_thai_consonant('A'));
    EXPECT_FALSE(ThaiUtils::is_thai_vowel('e'));
}

TEST_F(ThaiMetadataTest, TextProcessingUtilities) {
    string text_with_whitespace = "  สวัสดี   \t\n  ครับ  ";
    string normalized = ThaiUtils::normalize_whitespace(text_with_whitespace);
    EXPECT_EQ(normalized, "สวัสดี ครับ");
    
    string text_with_control = "Hello\x01\x02World\x1F";
    string cleaned = ThaiUtils::remove_control_characters(text_with_control);
    EXPECT_EQ(cleaned, "HelloWorld");
    
    EXPECT_TRUE(ThaiUtils::is_valid_utf8_sequence(thai_text_));
    EXPECT_FALSE(ThaiUtils::is_valid_utf8_sequence("\xFF\xFE"));
    
    size_t thai_count = ThaiUtils::count_thai_characters(mixed_text_);
    EXPECT_GT(thai_count, 0);
}

// Error Handling Tests
TEST_F(ThaiMetadataTest, HandleInvalidUTF8) {
    string invalid_utf8 = "\xFF\xFE\xInvalid";
    
    EXPECT_THROW({
        ThaiCharsetConverter::utf8_to_dab_thai(invalid_utf8);
    }, ThaiProcessingException);
}

TEST_F(ThaiMetadataTest, HandleLongText) {
    string very_long_text = string(10000, 'ก');
    
    EXPECT_NO_THROW({
        processor_->process_raw_metadata(very_long_text, "", "", "");
    });
}

TEST_F(ThaiMetadataTest, ExceptionErrorCodes) {
    try {
        ThaiCharsetConverter::utf8_to_dab_thai("\xFF\xFE");
        FAIL() << "Expected ThaiProcessingException";
    }
    catch (const ThaiProcessingException& e) {
        EXPECT_EQ(e.get_error_code(), ThaiProcessingError::InvalidUTF8);
        EXPECT_STREQ(e.what(), "Invalid UTF-8 sequence in input");
    }
}

// Performance Tests
TEST_F(ThaiMetadataTest, ProcessingPerformance) {
    auto start = chrono::high_resolution_clock::now();
    
    // Process 1000 metadata entries
    for (int i = 0; i < 1000; ++i) {
        processor_->process_raw_metadata(thai_song_title_, thai_artist_name_);
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 1000); // Should complete in less than 1 second
}

TEST_F(ThaiMetadataTest, MemoryUsage) {
    // Create many processors and ensure no memory leaks
    vector<unique_ptr<ThaiMetadataProcessor>> processors;
    
    for (int i = 0; i < 100; ++i) {
        processors.push_back(make_unique<ThaiMetadataProcessor>());
        processors.back()->process_raw_metadata(thai_song_title_, thai_artist_name_);
    }
    
    // Test should complete without memory issues
    EXPECT_EQ(processors.size(), 100);
}

// Edge Cases
TEST_F(ThaiMetadataTest, EmptyInput) {
    ThaiMetadata metadata = processor_->process_raw_metadata("", "", "", "");
    
    EXPECT_TRUE(metadata.title_utf8.empty());
    EXPECT_TRUE(metadata.artist_utf8.empty());
    EXPECT_FALSE(metadata.is_thai_content);
    EXPECT_EQ(metadata.thai_confidence, 0.0);
}

TEST_F(ThaiMetadataTest, SpecialCharacters) {
    string special_chars = "!@#$%^&*()_+-=[]{}|;:,.<>?";
    ThaiMetadata metadata = processor_->process_raw_metadata(special_chars, "", "", "");
    
    EXPECT_FALSE(metadata.is_thai_content);
    EXPECT_NO_THROW({
        processor_->generate_dls_from_metadata(metadata);
    });
}

TEST_F(ThaiMetadataTest, MixedScripts) {
    string mixed_scripts = "สวัสดี Hello مرحبا こんにちは";
    ThaiMetadata metadata = processor_->process_raw_metadata(mixed_scripts, "", "", "");
    
    auto stats = detector_->analyze_language_composition(mixed_scripts);
    EXPECT_GT(stats.thai_char_count, 0);
    EXPECT_GT(stats.total_char_count, stats.thai_char_count);
}