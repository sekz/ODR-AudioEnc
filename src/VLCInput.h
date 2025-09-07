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
#include <cstdint>

// Mock VLC Input class for testing
class VLCInput {
public:
    VLCInput(const std::string& url, int sample_rate, int channels, int buffer_ms);
    ~VLCInput();
    
    bool initialize(const std::vector<std::string>& options = {});
    bool open(const std::string& url);
    void close();
    
    ssize_t read(int16_t* buffer, size_t max_samples);
    
    std::string get_current_title() const;
    std::string get_current_artist() const;
    
    bool is_connected() const;
    int get_buffer_health() const;

private:
    std::string url_;
    int sample_rate_;
    int channels_;
    int buffer_ms_;
    bool connected_;
};