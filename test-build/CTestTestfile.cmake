# CMake generated Testfile for 
# Source directory: /home/seksan/workspace/streamdab/ODR-AudioEnc
# Build directory: /home/seksan/workspace/streamdab/ODR-AudioEnc/test-build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_enhanced_stream "/home/seksan/workspace/streamdab/ODR-AudioEnc/test-build/test_enhanced_stream")
set_tests_properties(test_enhanced_stream PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;157;add_test;/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;0;")
add_test(test_thai_metadata "/home/seksan/workspace/streamdab/ODR-AudioEnc/test-build/test_thai_metadata")
set_tests_properties(test_thai_metadata PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;157;add_test;/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;0;")
add_test(test_api_interface "/home/seksan/workspace/streamdab/ODR-AudioEnc/test-build/test_api_interface")
set_tests_properties(test_api_interface PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;157;add_test;/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;0;")
add_test(test_security_utils "/home/seksan/workspace/streamdab/ODR-AudioEnc/test-build/test_security_utils")
set_tests_properties(test_security_utils PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;157;add_test;/home/seksan/workspace/streamdab/ODR-AudioEnc/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
