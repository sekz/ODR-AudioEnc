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

/*! \file api_websocket.cpp
 *  \brief Minimal WebSocket server (RFC 6455) for the real-time StreamDAB updates.
 *
 *  It listens on its own port (ApiConfig::websocket_port, default HTTP port + 1),
 *  accepts text and binary messages from clients, and sends the server's
 *  MessagePack messages as binary frames. TLS is not supported; use a reverse proxy.
 */

#include "api_interface.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

namespace StreamDAB {

namespace {

constexpr size_t MAX_HANDSHAKE_BYTES = 8 * 1024;
constexpr size_t MAX_MESSAGE_BYTES = 1024 * 1024;
constexpr size_t MAX_CLIENTS = 256;

enum Opcode { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

// SHA-1 (FIPS 180-4). Only used for the handshake accept key, as RFC 6455 requires.
string sha1(const string& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    string msg = data;
    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) msg += static_cast<char>(0);
    for (int shift = 56; shift >= 0; shift -= 8) msg += static_cast<char>((bit_len >> shift) & 0xff);

    auto rol = [](uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); };

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 2])) << 8) |
                    static_cast<uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }

            const uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    string digest;
    for (uint32_t word : h) {
        for (int shift = 24; shift >= 0; shift -= 8) digest += static_cast<char>((word >> shift) & 0xff);
    }
    return digest;
}

string base64(const string& data) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    string out;
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                            static_cast<unsigned char>(data[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
    }
    if (i + 1 == data.size()) {
        const uint32_t v = static_cast<unsigned char>(data[i]) << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == data.size()) {
        const uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

string lower(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

bool send_all(int fd, const string& data) {
    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = send(fd, data.data() + written, data.size() - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

} // anonymous namespace

WebSocketServer::WebSocketServer(const ApiConfig& config, StreamDABApiInterface* api)
    : config_(config), api_(api) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

string WebSocketServer::generate_websocket_key_response(const string& key) {
    static const string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return base64(sha1(key + guid));
}

bool WebSocketServer::start() {
    if (running_) {
        return true;
    }

    const int port = config_.websocket_port > 0 ? config_.websocket_port : config_.port + 1;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, config_.bind_address.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 or
            listen(listen_fd_, 32) < 0) {
        fprintf(stderr, "WebSocket server cannot listen on %s:%d: %s\n",
                config_.bind_address.c_str(), port, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    printf("WebSocket server listening on port %d\n", port);

    running_ = true;
    server_thread_ = thread(&WebSocketServer::server_loop, this);
    return true;
}

void WebSocketServer::stop() {
    running_ = false;

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    // Tell the clients, then drop them
    vector<string> ids;
    {
        lock_guard<mutex> lock(clients_mutex_);
        for (auto& entry : clients_) {
            send_frame(entry.second.fd, Close, string("\x03\xe8", 2)); // 1000: normal closure
            close(entry.second.fd);
            ids.push_back(entry.first);
        }
        clients_.clear();
    }
    for (const auto& id : ids) {
        api_->handle_websocket_disconnect(id);
    }

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void WebSocketServer::server_loop() {
    while (running_) {
        vector<pollfd> fds;
        vector<string> ids;
        fds.push_back({listen_fd_, POLLIN, 0});
        {
            lock_guard<mutex> lock(clients_mutex_);
            for (const auto& entry : clients_) {
                fds.push_back({entry.second.fd, POLLIN, 0});
                ids.push_back(entry.first);
            }
        }

        // Wake up regularly so that stop() is noticed
        if (poll(fds.data(), fds.size(), 100) <= 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            accept_client();
        }

        for (size_t i = 0; i < ids.size(); ++i) {
            if (!(fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR))) {
                continue;
            }

            bool keep = false;
            {
                lock_guard<mutex> lock(clients_mutex_);
                auto it = clients_.find(ids[i]);
                if (it == clients_.end()) continue;

                char chunk[4096];
                const ssize_t n = recv(it->second.fd, chunk, sizeof(chunk), MSG_DONTWAIT);
                if (n > 0) {
                    it->second.buffer.append(chunk, static_cast<size_t>(n));
                    keep = process_client_data(it->second);
                }
                else if (n < 0 and (errno == EAGAIN or errno == EINTR)) {
                    keep = true;
                }
            }

            if (!keep) {
                drop_client(ids[i]);
            }
        }
    }
}

void WebSocketServer::accept_client() {
    sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    const int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_address), &client_len);
    if (fd < 0) {
        return;
    }

    {
        lock_guard<mutex> lock(clients_mutex_);
        if (clients_.size() >= MAX_CLIENTS) {
            close(fd);
            return;
        }
    }

    timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // The opening handshake is an HTTP upgrade request
    string request;
    char chunk[2048];
    while (request.find("\r\n\r\n") == string::npos) {
        if (request.size() > MAX_HANDSHAKE_BYTES) break;
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            close(fd);
            return;
        }
        request.append(chunk, static_cast<size_t>(n));
    }

    string upgrade, connection, key, version;
    istringstream stream(request);
    string line;
    getline(stream, line); // request line
    const bool is_get = line.compare(0, 4, "GET ") == 0;
    while (getline(stream, line)) {
        if (!line.empty() and line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == string::npos) continue;

        const string name = lower(line.substr(0, colon));
        string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        while (!value.empty() and (value.back() == ' ' or value.back() == '\t')) value.pop_back();

        if (name == "upgrade") upgrade = lower(value);
        else if (name == "connection") connection = lower(value);
        else if (name == "sec-websocket-key") key = value;
        else if (name == "sec-websocket-version") version = value;
    }

    if (!is_get or upgrade != "websocket" or connection.find("upgrade") == string::npos or
            key.empty() or version != "13") {
        send_all(fd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nSec-WebSocket-Version: 13\r\n"
                     "Content-Length: 0\r\n\r\n");
        close(fd);
        return;
    }

    const string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + generate_websocket_key_response(key) + "\r\n\r\n";
    if (!send_all(fd, response)) {
        close(fd);
        return;
    }

    Client client;
    client.fd = fd;
    client.id = ApiUtils::generate_secure_token(16);
    {
        lock_guard<mutex> lock(clients_mutex_);
        clients_[client.id] = client;
    }
    api_->handle_websocket_connection(client.id);
}

bool WebSocketServer::send_frame(int fd, int opcode, const string& payload) {
    // Frames from the server are never masked
    string frame;
    frame += static_cast<char>(0x80 | opcode); // FIN + opcode

    const size_t len = payload.size();
    if (len < 126) {
        frame += static_cast<char>(len);
    }
    else if (len < 65536) {
        frame += static_cast<char>(126);
        frame += static_cast<char>(len >> 8);
        frame += static_cast<char>(len & 0xff);
    }
    else {
        frame += static_cast<char>(127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame += static_cast<char>((static_cast<uint64_t>(len) >> shift) & 0xff);
        }
    }

    return send_all(fd, frame + payload);
}

// Decode every complete frame in the client's buffer. Returns false if the client
// must be dropped. Called with clients_mutex_ held.
bool WebSocketServer::process_client_data(Client& client) {
    while (true) {
        const string& buf = client.buffer;
        if (buf.size() < 2) return true;

        const unsigned char b0 = static_cast<unsigned char>(buf[0]);
        const unsigned char b1 = static_cast<unsigned char>(buf[1]);
        const bool fin = b0 & 0x80;
        const int opcode = b0 & 0x0f;
        const bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7f;
        size_t header = 2;

        if ((b0 & 0x70) != 0 or !masked) {
            return false; // reserved bits set, or an unmasked client frame: protocol error
        }
        if (len == 126) {
            if (buf.size() < 4) return true;
            len = (static_cast<unsigned char>(buf[2]) << 8) | static_cast<unsigned char>(buf[3]);
            header = 4;
        }
        else if (len == 127) {
            if (buf.size() < 10) return true;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<unsigned char>(buf[2 + i]);
            header = 10;
        }

        if (len > MAX_MESSAGE_BYTES) {
            return false;
        }
        if (buf.size() < header + 4 + len) return true; // incomplete frame

        const unsigned char* mask = reinterpret_cast<const unsigned char*>(buf.data()) + header;
        string payload(buf, header + 4, static_cast<size_t>(len));
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
        }
        client.buffer.erase(0, header + 4 + static_cast<size_t>(len));

        // Control frames (never fragmented, at most 125 bytes)
        if (opcode >= 0x8) {
            if (!fin or len > 125) return false;
            if (opcode == Close) {
                send_frame(client.fd, Close, payload.substr(0, 2));
                return false;
            }
            if (opcode == Ping) {
                if (!send_frame(client.fd, Pong, payload)) return false;
            }
            continue; // Pong: ignore
        }

        // Data frames, possibly fragmented
        if (opcode == Text or opcode == Binary) {
            if (!client.message.empty() or client.message_opcode != 0) return false; // new message mid-fragment
            client.message_opcode = opcode;
            client.message = payload;
        }
        else if (opcode == Continuation) {
            if (client.message_opcode == 0) return false;
            if (client.message.size() + payload.size() > MAX_MESSAGE_BYTES) return false;
            client.message += payload;
        }
        else {
            return false;
        }

        if (fin) {
            const string message = move(client.message);
            client.message.clear();
            client.message_opcode = 0;

            // The API takes its own locks; do not hold ours while it runs
            const string id = client.id;
            clients_mutex_.unlock();
            api_->handle_websocket_message(id, message);
            clients_mutex_.lock();
            if (clients_.find(id) == clients_.end()) return false;
        }
    }
}

void WebSocketServer::drop_client(const string& client_id) {
    {
        lock_guard<mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end()) return;
        close(it->second.fd);
        clients_.erase(it);
    }
    api_->handle_websocket_disconnect(client_id);
}

void WebSocketServer::broadcast_message(const WebSocketMessage& message) {
    // Subscriptions are kept by the API; look them up before taking our lock
    vector<string> ids;
    {
        lock_guard<mutex> lock(clients_mutex_);
        for (const auto& entry : clients_) ids.push_back(entry.first);
    }

    vector<string> failed;
    for (const auto& id : ids) {
        if (!message.client_id.empty() and message.client_id != id) continue;
        if (!api_->is_subscribed(id, message.type)) continue;

        lock_guard<mutex> lock(clients_mutex_);
        const auto it = clients_.find(id);
        if (it != clients_.end() and !send_frame(it->second.fd, Binary, message.data)) {
            failed.push_back(id);
        }
    }

    for (const auto& id : failed) {
        drop_client(id);
    }
}

void WebSocketServer::send_to_client(const string& client_id, const WebSocketMessage& message) {
    bool failed = false;
    {
        lock_guard<mutex> lock(clients_mutex_);
        const auto it = clients_.find(client_id);
        if (it == clients_.end()) return;
        failed = !send_frame(it->second.fd, Binary, message.data);
    }

    if (failed) {
        drop_client(client_id);
    }
}

} // namespace StreamDAB
