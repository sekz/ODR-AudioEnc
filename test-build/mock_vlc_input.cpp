// Mock VLC Input for testing
#include "VLCInput.h"
class VLCInput {
public:
    VLCInput(const std::string& url, int rate, int channels, int buffer_ms) {}
    bool initialize(const std::vector<std::string>& options) { return true; }
    bool open(const std::string& url) { return true; }
    ssize_t read(int16_t* buffer, size_t max_samples) { return 0; }
};
