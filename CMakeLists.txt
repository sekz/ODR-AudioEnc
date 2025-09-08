# Simplified CMakeLists.txt for testing ODR-AudioEnc enhancements
# This version works without external dependencies for demonstration

cmake_minimum_required(VERSION 3.16)
project(ODR-AudioEnc-Tests
    VERSION 3.6.0
    DESCRIPTION "ODR-AudioEnc StreamDAB Enhancements - Test Build"
    LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_COVERAGE "Enable coverage reporting" ON)

# Configure build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Enable static linking for better compatibility (user suggestion)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Building tests: ${BUILD_TESTS}")
message(STATUS "Static linking: Enabled for better compatibility")

# Find required packages
find_package(Threads REQUIRED)

# Try to find Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Use FetchContent to download GTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50f33f9277fd4dbdedb78b03e8a8c4.zip
        TIMEOUT 30
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

    if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/contrib
)

# Mock/stub sources for dependencies
set(MOCK_SOURCES
    src/mocks/mock_vlc_input.cpp
    src/mocks/mock_fdk_aac.cpp
)

# Core enhanced sources (without external dependencies)
set(ENHANCED_SOURCES
    src/enhanced_stream.cpp
    src/thai_metadata.cpp
    src/api_interface.cpp
    src/security_utils.cpp
)

# Create mock dependency files
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mock_vlc_input.cpp
"// Mock VLC Input for testing
#include \"VLCInput.h\"
class VLCInput {
public:
    VLCInput(const std::string& url, int rate, int channels, int buffer_ms) {}
    bool initialize(const std::vector<std::string>& options) { return true; }
    bool open(const std::string& url) { return true; }
    ssize_t read(int16_t* buffer, size_t max_samples) { return 0; }
};
")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mock_fdk_aac.cpp
"// Mock FDK-AAC for testing
extern \"C\" {
int aacEncoder_Open(void** phAacEncoder, int encModules, int maxChannels) { return 0; }
int aacEncoder_Close(void** phAacEncoder) { return 0; }
int aacEncoder_SetParam(void* hAacEncoder, int param, int value) { return 0; }
int aacEncoder_Encode(void* hAacEncoder, void* inBufDesc, void* outBufDesc, void* inargs, void* outargs) { return 0; }
}
")

# Test library
if(BUILD_TESTS)
    enable_testing()

    # Create a test library with core functionality
    add_library(odr_audioenc_core STATIC
        ${ENHANCED_SOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/mock_vlc_input.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/mock_fdk_aac.cpp
    )

    target_include_directories(odr_audioenc_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib
    )

    target_link_libraries(odr_audioenc_core
        Threads::Threads
    )

    target_compile_definitions(odr_audioenc_core PRIVATE
        STREAMDAB_ENHANCED
        VERSION="${PROJECT_VERSION}"
        MOCK_BUILD=1
    )

    # Individual test executables
    set(TEST_FILES
        tests/test_enhanced_stream.cpp
        tests/test_thai_metadata.cpp
        tests/test_api_interface.cpp
        tests/test_security_utils.cpp
    )

    foreach(TEST_FILE ${TEST_FILES})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_FILE})
            get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)

            add_executable(${TEST_NAME} ${TEST_FILE})

            target_link_libraries(${TEST_NAME}
                odr_audioenc_core
                gtest
                gtest_main
                gmock
                gmock_main
            )

            target_include_directories(${TEST_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
                ${CMAKE_CURRENT_SOURCE_DIR}/contrib
            )

            # Add test to CTest
            add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 30)
        endif()
    endforeach()

    # Coverage target
    if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        find_program(LCOV_PATH lcov)
        find_program(GENHTML_PATH genhtml)

        if(LCOV_PATH AND GENHTML_PATH)
            add_custom_target(coverage
                COMMAND ${LCOV_PATH} --directory . --zerocounters
                COMMAND ${CMAKE_CTEST_COMMAND} --verbose
                COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info
                COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/tests/*'
                    --output-file coverage_filtered.info
                COMMAND ${GENHTML_PATH} coverage_filtered.info --output-directory coverage_report
                COMMAND ${LCOV_PATH} --summary coverage_filtered.info
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating coverage report"
            )
        endif()
    endif()
endif()

message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Project Version: ${PROJECT_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Enable Coverage: ${ENABLE_COVERAGE}")
message(STATUS "")
