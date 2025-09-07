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
#include <iostream>

// Test environment setup
class ODRAudioEncTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::cout << "Setting up ODR-AudioEnc test environment..." << std::endl;
        
        // Initialize logging for tests
        // Set up temporary directories for test data
        // Initialize any global test resources
        
        std::cout << "Test environment initialized successfully" << std::endl;
    }
    
    void TearDown() override {
        std::cout << "Cleaning up ODR-AudioEnc test environment..." << std::endl;
        
        // Clean up temporary test files
        // Reset global state
        
        std::cout << "Test environment cleaned up" << std::endl;
    }
};

// Test fixture base class for common functionality
class ODRAudioEncTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup for all tests
        std::cout << "Setting up individual test..." << std::endl;
    }
    
    void TearDown() override {
        // Common cleanup for all tests
        std::cout << "Cleaning up individual test..." << std::endl;
    }
    
    // Helper methods for common test operations
    std::string getTestDataPath() const {
        return "test_data/";
    }
    
    std::string getTempPath() const {
        return "/tmp/odr_audioenc_tests/";
    }
};

int main(int argc, char **argv) {
    std::cout << "Starting ODR-AudioEnc Enhanced Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // Initialize Google Test
    ::testing::InitGoogleTest(&argc, argv);
    
    // Add global test environment
    ::testing::AddGlobalTestEnvironment(new ODRAudioEncTestEnvironment);
    
    // Run all tests
    int result = RUN_ALL_TESTS();
    
    std::cout << "Test suite completed with result: " << result << std::endl;
    return result;
}