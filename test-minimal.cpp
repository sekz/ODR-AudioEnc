// Minimal test to verify Google Test framework works in ODR-AudioEnc
#include <gtest/gtest.h>

// Simple test to verify Google Test is working
TEST(ODRAudioEncGoogleTest, BasicTest) {
    EXPECT_EQ(3 + 3, 6);
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
}

TEST(ODRAudioEncGoogleTest, AudioTest) {
    int sample_rate = 48000;
    int channels = 2;
    EXPECT_GT(sample_rate, 0);
    EXPECT_GT(channels, 0);
    EXPECT_EQ(sample_rate * channels, 96000);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}