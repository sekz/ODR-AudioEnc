# CMakeLists.txt for ODR-AudioEnc comprehensive testing framework
cmake_minimum_required(VERSION 3.10)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/contrib
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
)

# Test sources
set(TEST_SOURCES
    test_main.cpp
    test_thai_metadata.cpp
    test_enhanced_stream.cpp
    test_api_interface.cpp
    test_audio_processing.cpp
    test_security_utils.cpp
    test_etsi_compliance.cpp
    test_integration.cpp
    test_performance.cpp
)

# Production source files needed for testing
set(PRODUCTION_SOURCES
    ${CMAKE_SOURCE_DIR}/src/thai_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/enhanced_stream.cpp
    ${CMAKE_SOURCE_DIR}/src/api_interface.cpp
    ${CMAKE_SOURCE_DIR}/src/security_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils.cpp
    ${CMAKE_SOURCE_DIR}/contrib/Log.cpp
    ${CMAKE_SOURCE_DIR}/contrib/Socket.cpp
)

# Create test executable
add_executable(odr_audioenc_tests
    ${TEST_SOURCES}
    ${PRODUCTION_SOURCES}
)

# Link libraries
target_link_libraries(odr_audioenc_tests
    ${GTEST_LIBRARIES}
    ${GMOCK_LIBRARIES}
    pthread
    ssl
    crypto
    zmq
)

# Test discovery
enable_testing()
add_test(NAME ODR_AudioEnc_Tests COMMAND odr_audioenc_tests)

# Coverage target
if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    target_compile_options(odr_audioenc_tests PRIVATE --coverage)
    target_link_options(odr_audioenc_tests PRIVATE --coverage)
endif()
