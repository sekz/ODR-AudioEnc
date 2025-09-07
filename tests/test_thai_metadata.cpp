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
#include "../src/thai_metadata.h"
#include <chrono>

using namespace StreamDAB;
using namespace std::chrono;

// Test class for Thai character set conversion
class ThaiCharsetConverterTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ThaiCharsetConverterTest, ValidateThaiUTF8) {
    // Valid Thai UTF-8 strings
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("สวัสดี"));           // Hello
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("ขอบคุณ"));           // Thank you
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("เพลงไทย"));          // Thai music
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("Hello สวัสดี"));     // Mixed Thai-English
    
    // Valid ASCII
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("Hello World"));
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8("123456"));
    
    // Empty string
    EXPECT_TRUE(ThaiCharsetConverter::is_valid_thai_utf8(""));
    
    // Invalid UTF-8 sequences (these would be invalid byte sequences)
    // Note: These are conceptual - actual invalid bytes would need to be constructed
    std::string invalid_utf8;
    invalid_utf8.push_back(0xFF); // Invalid UTF-8 start byte
    invalid_utf8.push_back(0xFE);
    EXPECT_FALSE(ThaiCharsetConverter::is_valid_thai_utf8(invalid_utf8));
}

TEST_F(ThaiCharsetConverterTest, UTF8ToDABThaiConversion) {
    // Test basic Thai character conversion
    std::string thai_text = "ก"; // Thai character 'ก'
    std::string dab_encoded = ThaiCharsetConverter::utf8_to_dab_thai(thai_text);
    
    // Should not be empty and should be different from input
    EXPECT_FALSE(dab_encoded.empty());
    
    // Test ASCII passthrough
    std::string ascii_text = "Hello";
    std::string ascii_encoded = ThaiCharsetConverter::utf8_to_dab_thai(ascii_text);
    EXPECT_EQ(ascii_encoded, ascii_text); // ASCII should pass through unchanged
}

TEST_F(ThaiCharsetConverterTest, NormalizeThaiText) {
    // Test text normalization
    std::string input_with_extra_spaces = "  สวัสดี    ครับ  ";
    std::string normalized = ThaiCharsetConverter::normalize_thai_text(input_with_extra_spaces);
    
    // Should remove extra whitespace
    EXPECT_EQ(normalized, "สวัสดี ครับ");
    
    // Test control character removal
    std::string input_with_controls = "สวัสดี\x01\x02ครับ";
    normalized = ThaiCharsetConverter::normalize_thai_text(input_with_controls);
    EXPECT_EQ(normalized, "สวัสดีครับ");
}

TEST_F(ThaiCharsetConverterTest, CalculateThaiDisplayLength) {
    // Test display length calculation for Thai text
    std::string simple_thai = "กขค"; // 3 consonants
    EXPECT_EQ(ThaiCharsetConverter::calculate_thai_display_length(simple_thai), 3);
    
    // Test ASCII
    std::string ascii = "ABC";
    EXPECT_EQ(ThaiCharsetConverter::calculate_thai_display_length(ascii), 3);
    
    // Test empty string
    EXPECT_EQ(ThaiCharsetConverter::calculate_thai_display_length(""), 0);
    
    // Test Thai with combining characters (vowels, tone marks)
    // Note: This test assumes specific Thai combining character behavior
    std::string thai_with_combining = "กา"; // consonant + vowel
    size_t length = ThaiCharsetConverter::calculate_thai_display_length(thai_with_combining);
    EXPECT_GT(length, 0);
    EXPECT_LE(length, 2); // Should be at most 2 display positions
}

TEST_F(ThaiCharsetConverterTest, TruncateThaiText) {
    std::string long_thai = "สวัสดีครับผมชื่อสมชาย";
    
    // Truncate to 5 characters
    std::string truncated = ThaiCharsetConverter::truncate_thai_text(long_thai, 5);
    
    // Should be shorter than or equal to 5 display characters
    size_t display_length = ThaiCharsetConverter::calculate_thai_display_length(truncated);
    EXPECT_LE(display_length, 5);
    
    // Should not be empty unless input was empty
    EXPECT_FALSE(truncated.empty());
    
    // Test truncating already short text
    std::string short_thai = "สวัสดี";
    truncated = ThaiCharsetConverter::truncate_thai_text(short_thai, 10);
    EXPECT_EQ(truncated, short_thai); // Should remain unchanged
}

// Test class for Thai language detection
class ThaiLanguageDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        detector_ = std::make_unique<ThaiLanguageDetector>();
    }
    
    void TearDown() override {}
    
    std::unique_ptr<ThaiLanguageDetector> detector_;
};

TEST_F(ThaiLanguageDetectorTest, DetectThaiText) {
    // Pure Thai text
    EXPECT_TRUE(detector_->is_thai_text("สวัสดีครับ"));
    EXPECT_TRUE(detector_->is_thai_text("เพลงไทย"));
    EXPECT_TRUE(detector_->is_thai_text("ขอบคุณมาก"));
    
    // Pure English text
    EXPECT_FALSE(detector_->is_thai_text("Hello World"));
    EXPECT_FALSE(detector_->is_thai_text("English Song Title"));
    
    // Mixed text (should depend on Thai percentage)
    bool mixed_result = detector_->is_thai_text("Hello สวัสดี");
    // Result depends on implementation threshold
    
    // Empty text
    EXPECT_FALSE(detector_->is_thai_text(""));
    
    // Numbers only
    EXPECT_FALSE(detector_->is_thai_text("12345"));
}

TEST_F(ThaiLanguageDetectorTest, ThaiConfidenceScoring) {
    // Pure Thai should have high confidence
    double thai_confidence = detector_->get_thai_confidence("สวัสดีครับ");
    EXPECT_GT(thai_confidence, 0.8);
    
    // Pure English should have low confidence
    double english_confidence = detector_->get_thai_confidence("Hello World");
    EXPECT_LT(english_confidence, 0.2);
    
    // Mixed text should have intermediate confidence
    double mixed_confidence = detector_->get_thai_confidence("Hello สวัสดี");
    EXPECT_GT(mixed_confidence, 0.0);
    EXPECT_LT(mixed_confidence, 1.0);
    
    // Empty text should have zero confidence
    EXPECT_EQ(detector_->get_thai_confidence(""), 0.0);
}

TEST_F(ThaiLanguageDetectorTest, LanguageCompositionAnalysis) {
    // Analyze pure Thai text
    auto thai_stats = detector_->analyze_language_composition("สวัสดีครับ");
    EXPECT_GT(thai_stats.thai_char_count, 0);
    EXPECT_EQ(thai_stats.english_char_count, 0);
    EXPECT_GT(thai_stats.thai_percentage, 0.8);
    
    // Analyze pure English text
    auto english_stats = detector_->analyze_language_composition("Hello World");
    EXPECT_EQ(english_stats.thai_char_count, 0);
    EXPECT_GT(english_stats.english_char_count, 0);
    EXPECT_LT(english_stats.thai_percentage, 0.2);
    
    // Analyze mixed text
    auto mixed_stats = detector_->analyze_language_composition("Hello สวัสดี");
    EXPECT_GT(mixed_stats.thai_char_count, 0);
    EXPECT_GT(mixed_stats.english_char_count, 0);
    EXPECT_GT(mixed_stats.thai_percentage, 0.0);
    EXPECT_LT(mixed_stats.thai_percentage, 1.0);
}

// Test class for Buddhist calendar functionality
class BuddhistCalendarTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BuddhistCalendarTest, GregorianToBuddhistConversion) {
    // Test conversion of known dates
    auto buddhist_date = BuddhistCalendar::gregorian_to_buddhist(2024, 1, 15);
    
    EXPECT_TRUE(buddhist_date.is_valid);
    EXPECT_EQ(buddhist_date.year, 2567); // 2024 + 543
    EXPECT_EQ(buddhist_date.month, 1);
    EXPECT_EQ(buddhist_date.day, 15);
    EXPECT_FALSE(buddhist_date.thai_month_name.empty());
    
    // Test invalid dates
    auto invalid_date = BuddhistCalendar::gregorian_to_buddhist(2024, 13, 32);
    EXPECT_FALSE(invalid_date.is_valid);
}

TEST_F(BuddhistCalendarTest, ThaiMonthNames) {
    // Test Thai month name retrieval
    std::string january = BuddhistCalendar::get_thai_month_name(1);
    EXPECT_EQ(january, "มกราคม");
    
    std::string december = BuddhistCalendar::get_thai_month_name(12);
    EXPECT_EQ(december, "ธันวาคม");
    
    // Test invalid month
    std::string invalid_month = BuddhistCalendar::get_thai_month_name(13);
    EXPECT_TRUE(invalid_month.empty());
    
    std::string invalid_month_zero = BuddhistCalendar::get_thai_month_name(0);
    EXPECT_TRUE(invalid_month_zero.empty());
}

TEST_F(BuddhistCalendarTest, DateFormatting) {
    auto buddhist_date = BuddhistCalendar::gregorian_to_buddhist(2024, 1, 15);
    std::string formatted = BuddhistCalendar::format_buddhist_date(buddhist_date);
    
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find("2567"), std::string::npos); // Should contain Buddhist year
    EXPECT_NE(formatted.find("มกราคม"), std::string::npos); // Should contain Thai month name
    
    // Test invalid date formatting
    ThaiMetadata::BuddhistDate invalid_date;
    invalid_date.is_valid = false;
    std::string invalid_formatted = BuddhistCalendar::format_buddhist_date(invalid_date);
    EXPECT_TRUE(invalid_formatted.empty());
}

TEST_F(BuddhistCalendarTest, CurrentDateFormatting) {
    std::string current_date = BuddhistCalendar::format_current_buddhist_date();
    EXPECT_FALSE(current_date.empty());
    
    // Should contain "พ.ศ." (Buddhist Era marker)
    EXPECT_NE(current_date.find("พ.ศ."), std::string::npos);
}

TEST_F(BuddhistCalendarTest, DateValidation) {
    // Valid dates
    EXPECT_TRUE(BuddhistCalendar::is_valid_buddhist_date(2567, 1, 15));
    EXPECT_TRUE(BuddhistCalendar::is_valid_buddhist_date(2567, 12, 31));
    
    // Invalid dates
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(0, 1, 15));      // Invalid year
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 0, 15));   // Invalid month
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 13, 15));  // Invalid month
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 1, 0));    // Invalid day
    EXPECT_FALSE(BuddhistCalendar::is_valid_buddhist_date(2567, 1, 32));   // Invalid day
}

// Test class for DLS processor
class ThaiDLSProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        processor_ = std::make_unique<ThaiDLSProcessor>(MAX_DLS_LENGTH_THAI, true);
    }
    
    void TearDown() override {}
    
    std::unique_ptr<ThaiDLSProcessor> processor_;
};

TEST_F(ThaiDLSProcessorTest, ProcessShortThaiText) {
    std::string short_thai = "สวัสดี";
    auto dls_data = processor_->process_thai_text(short_thai);
    
    EXPECT_FALSE(dls_data.empty());
    
    // First byte should be charset indicator
    EXPECT_EQ(dls_data[0], DAB_THAI_CHARSET);
    
    // Should be able to validate the DLS content
    EXPECT_TRUE(processor_->validate_dls_content(dls_data));
}

TEST_F(ThaiDLSProcessorTest, ProcessEmptyText) {
    auto dls_data = processor_->process_thai_text("");
    
    // Should return empty vector for empty input
    EXPECT_TRUE(dls_data.empty());
}

TEST_F(ThaiDLSProcessorTest, ProcessLongThaiText) {
    // Create a long Thai text that exceeds DLS length limits
    std::string long_thai;
    for (int i = 0; i < 50; ++i) {
        long_thai += "สวัสดีครับผม";
    }
    
    auto dls_data = processor_->process_thai_text(long_thai);
    
    // Should not be empty but should be truncated to fit DLS limits
    EXPECT_FALSE(dls_data.empty());
    EXPECT_LE(dls_data.size(), MAX_DLS_LENGTH_THAI);
    
    // Should still be valid DLS content
    EXPECT_TRUE(processor_->validate_dls_content(dls_data));
}

TEST_F(ThaiDLSProcessorTest, ScrollingConfiguration) {
    // Test disabling scrolling
    processor_->set_scrolling(false, 0);
    
    std::string text = "สวัสดี";
    auto dls_data = processor_->process_thai_text(text);
    EXPECT_FALSE(dls_data.empty());
    
    // Test enabling scrolling with custom speed
    processor_->set_scrolling(true, 300);
    dls_data = processor_->process_thai_text(text);
    EXPECT_FALSE(dls_data.empty());
}

// Test class for Thai metadata processor
class ThaiMetadataProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        processor_ = std::make_unique<ThaiMetadataProcessor>();
    }
    
    void TearDown() override {}
    
    std::unique_ptr<ThaiMetadataProcessor> processor_;
};

TEST_F(ThaiMetadataProcessorTest, ProcessThaiMetadata) {
    std::string thai_title = "เพลงไทย";
    std::string thai_artist = "นักร้องไทย";
    std::string english_album = "Thai Collection";
    std::string station = "Radio Thailand";
    
    auto metadata = processor_->process_raw_metadata(thai_title, thai_artist, english_album, station);
    
    // Check basic fields
    EXPECT_EQ(metadata.title_utf8, thai_title);
    EXPECT_EQ(metadata.artist_utf8, thai_artist);
    EXPECT_EQ(metadata.album_utf8, english_album);
    EXPECT_EQ(metadata.station_utf8, station);
    
    // Should detect Thai content
    EXPECT_TRUE(metadata.is_thai_content);
    EXPECT_GT(metadata.thai_confidence, 0.5);
    
    // Should have valid timestamp
    auto now = system_clock::now();
    auto time_diff = duration_cast<seconds>(now - metadata.timestamp).count();
    EXPECT_GE(time_diff, 0);
    EXPECT_LE(time_diff, 5);
    
    // Should have valid Buddhist date
    EXPECT_TRUE(metadata.buddhist_date.is_valid);
}

TEST_F(ThaiMetadataProcessorTest, ProcessEnglishMetadata) {
    std::string english_title = "English Song";
    std::string english_artist = "English Artist";
    std::string english_album = "English Album";
    std::string station = "English Radio";
    
    auto metadata = processor_->process_raw_metadata(english_title, english_artist, english_album, station);
    
    // Should not detect Thai content
    EXPECT_FALSE(metadata.is_thai_content);
    EXPECT_LT(metadata.thai_confidence, 0.3);
    
    // DAB encoded fields might be empty or same as UTF-8 for non-Thai content
    // This depends on implementation
}

TEST_F(ThaiMetadataProcessorTest, GenerateDLSFromMetadata) {
    std::string thai_title = "เพลงไทย";
    std::string thai_artist = "นักร้องไทย";
    
    auto metadata = processor_->process_raw_metadata(thai_title, thai_artist);
    auto dls_data = processor_->generate_dls_from_metadata(metadata);
    
    EXPECT_FALSE(dls_data.empty());
    
    // Should start with charset indicator
    EXPECT_EQ(dls_data[0], DAB_THAI_CHARSET);
}

TEST_F(ThaiMetadataProcessorTest, ValidateMetadata) {
    auto metadata = processor_->process_raw_metadata("สวัสดี", "ครับ", "อัลบั้ม", "สถานี");
    
    EXPECT_TRUE(processor_->validate_metadata(metadata));
    
    // Test with invalid metadata (e.g., overly long fields)
    ThaiMetadata invalid_metadata;
    invalid_metadata.title_utf8 = std::string(2000, 'A'); // Too long
    EXPECT_FALSE(processor_->validate_metadata(invalid_metadata));
}

TEST_F(ThaiMetadataProcessorTest, ProcessingStatistics) {
    // Process some metadata
    processor_->process_raw_metadata("เพลงไทย", "นักร้อง");
    processor_->process_raw_metadata("English Song", "Artist");
    processor_->process_raw_metadata("ไทย + English", "Mixed");
    
    auto stats = processor_->get_processing_stats();
    
    EXPECT_EQ(stats.total_metadata_processed, 3);
    EXPECT_GT(stats.thai_content_detected, 0);
    EXPECT_LE(stats.thai_content_detected, 3);
    EXPECT_EQ(stats.conversion_errors, 0);
    
    // Reset and verify
    processor_->reset_stats();
    stats = processor_->get_processing_stats();
    EXPECT_EQ(stats.total_metadata_processed, 0);
    EXPECT_EQ(stats.thai_content_detected, 0);
}

// Thai utility function tests
class ThaiUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ThaiUtilsTest, UTF8CodepointConversion) {
    std::string thai_text = "สวัสดี";
    auto codepoints = ThaiUtils::utf8_to_codepoints(thai_text);
    
    EXPECT_FALSE(codepoints.empty());
    
    // Convert back to UTF-8
    std::string converted_back = ThaiUtils::codepoints_to_utf8(codepoints);
    EXPECT_EQ(converted_back, thai_text);
    
    // Test with ASCII
    std::string ascii = "Hello";
    auto ascii_codepoints = ThaiUtils::utf8_to_codepoints(ascii);
    EXPECT_EQ(ascii_codepoints.size(), 5);
    
    std::string ascii_back = ThaiUtils::codepoints_to_utf8(ascii_codepoints);
    EXPECT_EQ(ascii_back, ascii);
}

TEST_F(ThaiUtilsTest, ThaiCharacterClassification) {
    // Thai consonant
    uint32_t thai_consonant = 0x0E01; // ก
    EXPECT_TRUE(ThaiUtils::is_thai_consonant(thai_consonant));
    EXPECT_FALSE(ThaiUtils::is_thai_vowel(thai_consonant));
    EXPECT_FALSE(ThaiUtils::is_thai_tone_mark(thai_consonant));
    
    // Thai vowel
    uint32_t thai_vowel = 0x0E32; // า
    EXPECT_FALSE(ThaiUtils::is_thai_consonant(thai_vowel));
    EXPECT_TRUE(ThaiUtils::is_thai_vowel(thai_vowel));
    EXPECT_FALSE(ThaiUtils::is_thai_tone_mark(thai_vowel));
    
    // Thai tone mark
    uint32_t thai_tone = 0x0E48; // ่
    EXPECT_FALSE(ThaiUtils::is_thai_consonant(thai_tone));
    EXPECT_FALSE(ThaiUtils::is_thai_vowel(thai_tone));
    EXPECT_TRUE(ThaiUtils::is_thai_tone_mark(thai_tone));
    
    // Thai number
    uint32_t thai_number = 0x0E50; // ๐
    EXPECT_TRUE(ThaiUtils::is_thai_number(thai_number));
    
    // ASCII character
    uint32_t ascii_char = 0x41; // A
    EXPECT_FALSE(ThaiUtils::is_thai_consonant(ascii_char));
    EXPECT_FALSE(ThaiUtils::is_thai_vowel(ascii_char));
    EXPECT_FALSE(ThaiUtils::is_thai_tone_mark(ascii_char));
    EXPECT_FALSE(ThaiUtils::is_thai_number(ascii_char));
}

TEST_F(ThaiUtilsTest, TextNormalization) {
    // Test whitespace normalization
    std::string input_with_spaces = "  Hello   World  ";
    std::string normalized = ThaiUtils::normalize_whitespace(input_with_spaces);
    EXPECT_EQ(normalized, "Hello World");
    
    // Test control character removal
    std::string input_with_controls = "Hello\x01\x02World";
    std::string cleaned = ThaiUtils::remove_control_characters(input_with_controls);
    EXPECT_EQ(cleaned, "HelloWorld");
}

TEST_F(ThaiUtilsTest, UTF8Validation) {
    // Valid UTF-8
    EXPECT_TRUE(ThaiUtils::is_valid_utf8_sequence("Hello สวัสดี"));
    EXPECT_TRUE(ThaiUtils::is_valid_utf8_sequence(""));
    EXPECT_TRUE(ThaiUtils::is_valid_utf8_sequence("ASCII only"));
    
    // Invalid UTF-8 sequence
    std::string invalid_utf8;
    invalid_utf8.push_back(0xFF);
    invalid_utf8.push_back(0xFE);
    EXPECT_FALSE(ThaiUtils::is_valid_utf8_sequence(invalid_utf8));
}

TEST_F(ThaiUtilsTest, ThaiCharacterCounting) {
    std::string mixed_text = "Hello สวัสดี World ครับ";
    size_t thai_count = ThaiUtils::count_thai_characters(mixed_text);
    
    EXPECT_GT(thai_count, 0);
    EXPECT_LT(thai_count, mixed_text.length()); // Should be less than total length
    
    // Pure English
    EXPECT_EQ(ThaiUtils::count_thai_characters("Hello World"), 0);
    
    // Pure Thai
    EXPECT_GT(ThaiUtils::count_thai_characters("สวัสดี"), 0);
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}