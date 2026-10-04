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

/*! \file api_serialization.cpp
 *  \brief MessagePack encoding for the WebSocket protocol, and decoding of the
 *         incoming messages (MessagePack from WebSocket clients, JSON from the
 *         HTTP API).
 */

#include "api_interface.h"
#include <cmath>
#include <cstring>

using namespace std;
using namespace std::chrono;

namespace StreamDAB {

namespace {

// ---------------------------------------------------------------------
// A small value tree, filled by both the JSON and the MessagePack reader

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Map } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    string text;
    vector<Value> array;
    map<string, Value> object;
};

constexpr int MAX_DEPTH = 16;

// ---------------------------------------------------------------------
// JSON reader (RFC 8259, without surrogate pair handling beyond the BMP)

class JsonReader {
public:
    explicit JsonReader(const string& text) : text_(text) {}

    bool parse(Value& out) {
        skip_space();
        if (!parse_value(out, 0)) return false;
        skip_space();
        return pos_ == text_.size();
    }

private:
    const string& text_;
    size_t pos_ = 0;

    void skip_space() {
        while (pos_ < text_.size() and
                (text_[pos_] == ' ' or text_[pos_] == '\t' or text_[pos_] == '\n' or text_[pos_] == '\r')) {
            pos_++;
        }
    }

    bool consume(const char* literal) {
        const size_t len = strlen(literal);
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > MAX_DEPTH or pos_ >= text_.size()) return false;

        const char c = text_[pos_];
        if (c == '{') return parse_object(out, depth);
        if (c == '[') return parse_array(out, depth);
        if (c == '"') {
            out.type = Value::Type::String;
            return parse_string(out.text);
        }
        if (consume("true")) { out.type = Value::Type::Bool; out.boolean = true; return true; }
        if (consume("false")) { out.type = Value::Type::Bool; out.boolean = false; return true; }
        if (consume("null")) { out.type = Value::Type::Null; return true; }
        return parse_number(out);
    }

    bool parse_number(Value& out) {
        const char* start = text_.c_str() + pos_;
        char* end = nullptr;
        const double value = strtod(start, &end);
        if (end == start or !isfinite(value)) return false;
        pos_ += static_cast<size_t>(end - start);
        out.type = Value::Type::Number;
        out.number = value;
        return true;
    }

    static void append_utf8(string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parse_string(string& out) {
        if (text_[pos_] != '"') return false;
        pos_++;
        out.clear();

        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false; // raw control characters are not allowed
            if (c != '\\') { out += static_cast<char>(c); continue; }

            if (pos_ >= text_.size()) return false;
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) return false;
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        cp <<= 4;
                        if (h >= '0' and h <= '9') cp |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' and h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' and h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
                        else return false;
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false; // unterminated
    }

    bool parse_array(Value& out, int depth) {
        out.type = Value::Type::Array;
        pos_++; // [
        skip_space();
        if (pos_ < text_.size() and text_[pos_] == ']') { pos_++; return true; }

        while (true) {
            Value element;
            skip_space();
            if (!parse_value(element, depth + 1)) return false;
            out.array.push_back(move(element));
            skip_space();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == ',') { pos_++; continue; }
            if (text_[pos_] == ']') { pos_++; return true; }
            return false;
        }
    }

    bool parse_object(Value& out, int depth) {
        out.type = Value::Type::Map;
        pos_++; // {
        skip_space();
        if (pos_ < text_.size() and text_[pos_] == '}') { pos_++; return true; }

        while (true) {
            skip_space();
            string key;
            if (pos_ >= text_.size() or !parse_string(key)) return false;
            skip_space();
            if (pos_ >= text_.size() or text_[pos_] != ':') return false;
            pos_++;
            skip_space();

            Value element;
            if (!parse_value(element, depth + 1)) return false;
            out.object[key] = move(element);

            skip_space();
            if (pos_ >= text_.size()) return false;
            if (text_[pos_] == ',') { pos_++; continue; }
            if (text_[pos_] == '}') { pos_++; return true; }
            return false;
        }
    }
};

// ---------------------------------------------------------------------
// MessagePack reader (the subset a client can reasonably send: maps, arrays,
// strings, booleans, nil, integers and floats)

class MsgPackReader {
public:
    explicit MsgPackReader(const string& data) : data_(data) {}

    bool parse(Value& out) {
        return read_value(out, 0) and pos_ == data_.size();
    }

private:
    const string& data_;
    size_t pos_ = 0;

    bool need(size_t n) const { return data_.size() - pos_ >= n; }

    bool read_uint(size_t bytes, uint64_t& out) {
        if (!need(bytes)) return false;
        out = 0;
        for (size_t i = 0; i < bytes; ++i) {
            out = (out << 8) | static_cast<unsigned char>(data_[pos_++]);
        }
        return true;
    }

    bool read_string(size_t length, Value& out) {
        if (!need(length)) return false;
        out.type = Value::Type::String;
        out.text = data_.substr(pos_, length);
        pos_ += length;
        return true;
    }

    bool read_array(size_t count, Value& out, int depth) {
        out.type = Value::Type::Array;
        if (count > data_.size() - pos_) return false; // every element takes at least one byte
        for (size_t i = 0; i < count; ++i) {
            Value element;
            if (!read_value(element, depth + 1)) return false;
            out.array.push_back(move(element));
        }
        return true;
    }

    bool read_map(size_t count, Value& out, int depth) {
        out.type = Value::Type::Map;
        if (count > (data_.size() - pos_) / 2) return false;
        for (size_t i = 0; i < count; ++i) {
            Value key, element;
            if (!read_value(key, depth + 1) or key.type != Value::Type::String) return false;
            if (!read_value(element, depth + 1)) return false;
            out.object[key.text] = move(element);
        }
        return true;
    }

    bool read_value(Value& out, int depth) {
        if (depth > MAX_DEPTH or !need(1)) return false;
        const unsigned char tag = static_cast<unsigned char>(data_[pos_++]);
        uint64_t n = 0;

        if (tag <= 0x7f) { out.type = Value::Type::Number; out.number = tag; return true; }
        if (tag >= 0xe0) { out.type = Value::Type::Number; out.number = static_cast<int8_t>(tag); return true; }
        if ((tag & 0xe0) == 0xa0) return read_string(tag & 0x1f, out);
        if ((tag & 0xf0) == 0x90) return read_array(tag & 0x0f, out, depth);
        if ((tag & 0xf0) == 0x80) return read_map(tag & 0x0f, out, depth);

        switch (tag) {
            case 0xc0: out.type = Value::Type::Null; return true;
            case 0xc2: out.type = Value::Type::Bool; out.boolean = false; return true;
            case 0xc3: out.type = Value::Type::Bool; out.boolean = true; return true;

            case 0xca: { // float32
                if (!read_uint(4, n)) return false;
                const uint32_t bits = static_cast<uint32_t>(n);
                float f;
                memcpy(&f, &bits, sizeof(f));
                out.type = Value::Type::Number; out.number = f;
                return true;
            }
            case 0xcb: { // float64
                if (!read_uint(8, n)) return false;
                double d;
                memcpy(&d, &n, sizeof(d));
                out.type = Value::Type::Number; out.number = d;
                return true;
            }
            case 0xcc: case 0xcd: case 0xce: case 0xcf: { // uint 8/16/32/64
                if (!read_uint(size_t(1) << (tag - 0xcc), n)) return false;
                out.type = Value::Type::Number; out.number = static_cast<double>(n);
                return true;
            }
            case 0xd0: case 0xd1: case 0xd2: case 0xd3: { // int 8/16/32/64
                const size_t bytes = size_t(1) << (tag - 0xd0);
                if (!read_uint(bytes, n)) return false;
                int64_t v;
                switch (bytes) {
                    case 1: v = static_cast<int8_t>(n); break;
                    case 2: v = static_cast<int16_t>(n); break;
                    case 4: v = static_cast<int32_t>(n); break;
                    default: v = static_cast<int64_t>(n); break;
                }
                out.type = Value::Type::Number; out.number = static_cast<double>(v);
                return true;
            }
            case 0xd9: return read_uint(1, n) and read_string(n, out);
            case 0xda: return read_uint(2, n) and read_string(n, out);
            case 0xdb: return read_uint(4, n) and read_string(n, out);
            case 0xdc: return read_uint(2, n) and read_array(n, out, depth);
            case 0xdd: return read_uint(4, n) and read_array(n, out, depth);
            case 0xde: return read_uint(2, n) and read_map(n, out, depth);
            case 0xdf: return read_uint(4, n) and read_map(n, out, depth);
            default: return false; // binary, ext, ... are not part of the protocol
        }
    }
};

//! Decode a client message: JSON if it starts with '{', MessagePack otherwise
bool decode_message(const string& data, Value& out) {
    const size_t first = data.find_first_not_of(" \t\r\n");
    if (first == string::npos) return false;

    if (data[first] == '{') {
        return JsonReader(data).parse(out) and out.type == Value::Type::Map;
    }
    return MsgPackReader(data).parse(out) and out.type == Value::Type::Map;
}

} // anonymous namespace

// ---------------------------------------------------------------------
// MessagePack writer

string MessagePackSerializer::pack_string(const string& str) {
    string out;
    const size_t len = str.size();
    if (len < 32) {
        out += static_cast<char>(0xa0 | len);
    }
    else if (len < 256) {
        out += static_cast<char>(0xd9);
        out += static_cast<char>(len);
    }
    else if (len < 65536) {
        out += static_cast<char>(0xda);
        out += static_cast<char>(len >> 8);
        out += static_cast<char>(len & 0xff);
    }
    else {
        out += static_cast<char>(0xdb);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((len >> shift) & 0xff);
        }
    }
    return out + str;
}

string MessagePackSerializer::pack_map_header(size_t count) {
    string out;
    if (count < 16) {
        out += static_cast<char>(0x80 | count);
    }
    else if (count < 65536) {
        out += static_cast<char>(0xde);
        out += static_cast<char>(count >> 8);
        out += static_cast<char>(count & 0xff);
    }
    else {
        out += static_cast<char>(0xdf);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((count >> shift) & 0xff);
        }
    }
    return out;
}

string MessagePackSerializer::pack_map(const map<string, string>& data) {
    string out = pack_map_header(data.size());
    for (const auto& entry : data) {
        out += pack_string(entry.first);
        out += pack_string(entry.second);
    }
    return out;
}

string MessagePackSerializer::pack_array(const vector<string>& data) {
    string out;
    const size_t count = data.size();
    if (count < 16) {
        out += static_cast<char>(0x90 | count);
    }
    else if (count < 65536) {
        out += static_cast<char>(0xdc);
        out += static_cast<char>(count >> 8);
        out += static_cast<char>(count & 0xff);
    }
    else {
        out += static_cast<char>(0xdd);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((count >> shift) & 0xff);
        }
    }
    for (const auto& element : data) {
        out += pack_string(element);
    }
    return out;
}

string MessagePackSerializer::pack_uint(uint64_t value) {
    string out;
    if (value < 128) {
        out += static_cast<char>(value);
    }
    else if (value <= 0xff) {
        out += static_cast<char>(0xcc);
        out += static_cast<char>(value);
    }
    else if (value <= 0xffff) {
        out += static_cast<char>(0xcd);
        out += static_cast<char>(value >> 8);
        out += static_cast<char>(value & 0xff);
    }
    else if (value <= 0xffffffffULL) {
        out += static_cast<char>(0xce);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((value >> shift) & 0xff);
        }
    }
    else {
        out += static_cast<char>(0xcf);
        for (int shift = 56; shift >= 0; shift -= 8) {
            out += static_cast<char>((value >> shift) & 0xff);
        }
    }
    return out;
}

string MessagePackSerializer::pack_double(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));

    string out(1, static_cast<char>(0xcb));
    for (int shift = 56; shift >= 0; shift -= 8) {
        out += static_cast<char>((bits >> shift) & 0xff);
    }
    return out;
}

string MessagePackSerializer::pack_bool(bool value) {
    return string(1, static_cast<char>(value ? 0xc3 : 0xc2));
}

// MessagePack timestamp extension (type -1), in the shortest of its three formats
string MessagePackSerializer::pack_timestamp(const system_clock::time_point& time) {
    const auto since_epoch = time.time_since_epoch();
    int64_t seconds_part = duration_cast<seconds>(since_epoch).count();
    int64_t nanos = duration_cast<nanoseconds>(since_epoch).count() - seconds_part * 1000000000LL;
    if (nanos < 0) { // keep nanoseconds in 0..999999999 for times before 1970
        nanos += 1000000000LL;
        seconds_part -= 1;
    }

    string out;
    if (nanos == 0 and seconds_part >= 0 and seconds_part <= 0xffffffffLL) {
        out += static_cast<char>(0xd6); // fixext 4
        out += static_cast<char>(0xff);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((seconds_part >> shift) & 0xff);
        }
    }
    else if (seconds_part >= 0 and seconds_part < (int64_t(1) << 34)) {
        const uint64_t packed = (static_cast<uint64_t>(nanos) << 34) | static_cast<uint64_t>(seconds_part);
        out += static_cast<char>(0xd7); // fixext 8
        out += static_cast<char>(0xff);
        for (int shift = 56; shift >= 0; shift -= 8) {
            out += static_cast<char>((packed >> shift) & 0xff);
        }
    }
    else {
        out += static_cast<char>(0xc7); // ext 8, 12 bytes
        out += static_cast<char>(12);
        out += static_cast<char>(0xff);
        for (int shift = 24; shift >= 0; shift -= 8) {
            out += static_cast<char>((nanos >> shift) & 0xff);
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            out += static_cast<char>((static_cast<uint64_t>(seconds_part) >> shift) & 0xff);
        }
    }
    return out;
}

string MessagePackSerializer::serialize_status(const StreamQualityMetrics& metrics,
                                               const ThaiMetadata& metadata) {
    vector<pair<string, string>> e;
    e.emplace_back("type", pack_string("status"));
    e.emplace_back("snr_db", pack_double(metrics.snr_db));
    e.emplace_back("volume_peak", pack_double(metrics.volume_peak));
    e.emplace_back("volume_rms", pack_double(metrics.volume_rms));
    e.emplace_back("buffer_health", pack_uint(metrics.buffer_health < 0 ? 0 : metrics.buffer_health));
    e.emplace_back("is_silence", pack_bool(metrics.is_silence));
    e.emplace_back("reconnect_count", pack_uint(metrics.reconnect_count));
    e.emplace_back("underrun_count", pack_uint(metrics.underrun_count));
    e.emplace_back("title", pack_string(metadata.title_utf8));
    e.emplace_back("artist", pack_string(metadata.artist_utf8));
    e.emplace_back("is_thai_content", pack_bool(metadata.is_thai_content));
    e.emplace_back("timestamp", pack_timestamp(metadata.timestamp));

    string out = pack_map_header(e.size());
    for (const auto& entry : e) out += pack_string(entry.first) + entry.second;
    return out;
}

string MessagePackSerializer::serialize_metadata(const ThaiMetadata& metadata) {
    vector<pair<string, string>> e;
    e.emplace_back("type", pack_string("metadata"));
    e.emplace_back("title_utf8", pack_string(metadata.title_utf8));
    e.emplace_back("artist_utf8", pack_string(metadata.artist_utf8));
    e.emplace_back("album_utf8", pack_string(metadata.album_utf8));
    e.emplace_back("station_utf8", pack_string(metadata.station_utf8));
    e.emplace_back("is_thai_content", pack_bool(metadata.is_thai_content));
    e.emplace_back("thai_confidence", pack_double(metadata.thai_confidence));
    e.emplace_back("timestamp", pack_timestamp(metadata.timestamp));

    string out = pack_map_header(e.size());
    for (const auto& entry : e) out += pack_string(entry.first) + entry.second;
    return out;
}

string MessagePackSerializer::serialize_quality_metrics(const StreamQualityMetrics& metrics) {
    vector<pair<string, string>> e;
    e.emplace_back("type", pack_string("quality_metrics"));
    e.emplace_back("snr_db", pack_double(metrics.snr_db));
    e.emplace_back("volume_peak", pack_double(metrics.volume_peak));
    e.emplace_back("volume_rms", pack_double(metrics.volume_rms));
    e.emplace_back("buffer_health", pack_uint(metrics.buffer_health < 0 ? 0 : metrics.buffer_health));
    e.emplace_back("is_silence", pack_bool(metrics.is_silence));
    e.emplace_back("reconnect_count", pack_uint(metrics.reconnect_count));
    e.emplace_back("underrun_count", pack_uint(metrics.underrun_count));

    string out = pack_map_header(e.size());
    for (const auto& entry : e) out += pack_string(entry.first) + entry.second;
    return out;
}

string MessagePackSerializer::serialize_stream_info(const string& url, const string& format,
                                                    const string& bitrate) {
    return pack_map({{"type", "stream_info"}, {"url", url}, {"format", format}, {"bitrate", bitrate}});
}

string MessagePackSerializer::serialize_error(const string& error_message, const string& error_code) {
    return pack_map({{"type", "error"}, {"error", error_message}, {"code", error_code}});
}

// ---------------------------------------------------------------------
// Incoming messages

MessagePackSerializer::ConfigUpdate MessagePackSerializer::deserialize_config_update(const string& data) {
    ConfigUpdate update;

    Value root;
    if (!decode_message(data, root)) {
        return update;
    }

    const auto url_it = root.object.find("primary_url");
    if (url_it == root.object.end() or url_it->second.type != Value::Type::String) {
        return update;
    }
    update.primary_url = url_it->second.text;
    if (!ApiUtils::is_valid_stream_url(update.primary_url)) {
        return update;
    }

    const auto fallback_it = root.object.find("fallback_urls");
    if (fallback_it != root.object.end()) {
        if (fallback_it->second.type != Value::Type::Array) {
            return update;
        }
        for (const auto& element : fallback_it->second.array) {
            if (element.type != Value::Type::String or !ApiUtils::is_valid_stream_url(element.text)) {
                return update;
            }
            update.fallback_urls.push_back(element.text);
        }
    }

    const auto norm_it = root.object.find("enable_normalization");
    if (norm_it != root.object.end()) {
        if (norm_it->second.type != Value::Type::Bool) {
            return update;
        }
        update.enable_normalization = norm_it->second.boolean;
    }

    const auto level_it = root.object.find("target_level_db");
    if (level_it != root.object.end()) {
        if (level_it->second.type != Value::Type::Number or
                level_it->second.number < -60.0 or level_it->second.number > 0.0) {
            return update;
        }
        update.target_level_db = level_it->second.number;
    }

    update.is_valid = true;
    return update;
}

MessagePackSerializer::Subscription MessagePackSerializer::deserialize_subscription(const string& data) {
    Subscription subscription;

    Value root;
    if (!decode_message(data, root)) {
        return subscription;
    }

    for (const char* key : {"subscribe", "unsubscribe"}) {
        const auto it = root.object.find(key);
        if (it == root.object.end()) {
            continue;
        }
        if (it->second.type != Value::Type::String) {
            return subscription;
        }

        const string& topic = it->second.text;
        if (topic != "status" and topic != "metadata" and topic != "metrics" and topic != "all") {
            return subscription;
        }

        subscription.topic = topic;
        subscription.enable = (string(key) == "subscribe");
        subscription.is_valid = true;
        return subscription;
    }

    return subscription;
}

// ---------------------------------------------------------------------

namespace ApiUtils {

bool is_valid_stream_url(const string& url) {
    return StreamUtils::validate_stream_url(url);
}

} // namespace ApiUtils

} // namespace StreamDAB
