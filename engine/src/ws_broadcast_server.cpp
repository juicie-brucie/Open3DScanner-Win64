#include "openscanner/ws_broadcast_server.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define OPENSCANNER_INVALID_SOCKET INVALID_SOCKET
#define OPENSCANNER_CLOSESOCK closesocket
#define OPENSCANNER_SHUTDOWN(s) shutdown(s, SD_BOTH)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define OPENSCANNER_INVALID_SOCKET (-1)
#define OPENSCANNER_CLOSESOCK ::close
#define OPENSCANNER_SHUTDOWN(s) shutdown(s, SHUT_RDWR)
#endif

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-4) and Base64 — only used to compute the Sec-WebSocket-
// Accept header during the RFC 6455 opening handshake. Implemented
// directly from the published specifications; not derived from any
// third-party source.
// ---------------------------------------------------------------------------
namespace {

struct Sha1Digest {
    uint8_t bytes[20];
};

uint32_t RotLeft(uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

Sha1Digest Sha1(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<uint8_t> msg(data, data + len);
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = RotLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = RotLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotLeft(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    Sha1Digest digest;
    uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest.bytes[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest.bytes[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest.bytes[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest.bytes[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
        i += 3;
    }
    size_t remaining = len - i;
    if (remaining == 1) {
        uint32_t n = data[i] << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string ComputeAcceptKey(const std::string& client_key) {
    std::string combined = client_key + kWebSocketGuid;
    Sha1Digest digest = Sha1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
    return Base64Encode(digest.bytes, sizeof(digest.bytes));
}

// Case-insensitive search for a header's value in a raw HTTP request.
bool FindHeaderValue(const std::string& request, const std::string& header_name, std::string* out_value) {
    std::string lower_request = request;
    std::string lower_header = header_name;
    std::transform(lower_request.begin(), lower_request.end(), lower_request.begin(), ::tolower);
    std::transform(lower_header.begin(), lower_header.end(), lower_header.begin(), ::tolower);

    size_t pos = lower_request.find(lower_header + ":");
    if (pos == std::string::npos) return false;
    size_t value_start = pos + header_name.size() + 1;
    while (value_start < request.size() && (request[value_start] == ' ' || request[value_start] == '\t')) {
        ++value_start;
    }
    size_t value_end = request.find("\r\n", value_start);
    if (value_end == std::string::npos) return false;
    *out_value = request.substr(value_start, value_end - value_start);
    return true;
}

bool RecvExact(socket_t sock, uint8_t* out, size_t len) {
    size_t got = 0;
    while (got < len) {
#if defined(_WIN32)
        int n = recv(sock, reinterpret_cast<char*>(out + got), static_cast<int>(len - got), 0);
#else
        ssize_t n = recv(sock, out + got, len - got, 0);
#endif
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

// Decodes exactly one incoming WebSocket frame. Rejects (returns false)
// anything this server doesn't need to handle: fragmented frames
// (fin=0) and payloads over 64KiB — real control commands are a few
// dozen bytes of JSON, so there's nothing legitimate on either side of
// those limits, only a misbehaving or hostile client.
bool ReadFrame(socket_t client, uint8_t* opcode, std::vector<uint8_t>* payload) {
    uint8_t head[2];
    if (!RecvExact(client, head, sizeof(head))) return false;
    bool fin = (head[0] & 0x80) != 0;
    *opcode = head[0] & 0x0F;
    bool masked = (head[1] & 0x80) != 0;
    uint64_t len = head[1] & 0x7F;
    if (len == 126) {
        uint8_t ext[2];
        if (!RecvExact(client, ext, sizeof(ext))) return false;
        len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (!RecvExact(client, ext, sizeof(ext))) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }
    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (!RecvExact(client, mask_key, sizeof(mask_key))) return false;
    }
    if (!fin || len > 65536) return false;
    payload->resize(static_cast<size_t>(len));
    if (len > 0 && !RecvExact(client, payload->data(), static_cast<size_t>(len))) return false;
    if (masked) {
        for (size_t i = 0; i < payload->size(); ++i) {
            (*payload)[i] ^= mask_key[i % 4];
        }
    }
    return true;
}

bool SendAll(socket_t sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#if defined(_WIN32)
        int n = send(sock, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
#else
#if defined(MSG_NOSIGNAL)
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;  // macOS: SO_NOSIGPIPE + process SIGPIPE ignore cover this.
#endif
        ssize_t n = send(sock, data + sent, len - sent, flags);
#endif
        if (n <= 0) return false;  // Failed/timed-out send: caller drops this client.
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Encodes and sends one unmasked WebSocket frame (server->client frames are
// never masked per RFC 6455). Returns false if the peer stalled/gone; the
// caller must not write another frame to that socket.
bool SendFrame(socket_t sock, uint8_t opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> header;
    header.push_back(0x80 | opcode);  // FIN=1, opcode
    if (len <= 125) {
        header.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        header.push_back(126);
        header.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        header.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        header.push_back(127);
        for (int i = 7; i >= 0; --i) {
            header.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    if (!SendAll(sock, header.data(), header.size())) return false;
    if (len > 0 && !SendAll(sock, payload, len)) return false;
    return true;
}

}  // namespace

namespace openscanner {

struct WsBroadcastServer::Impl {
    std::atomic<bool> running{false};
    socket_t listen_socket = OPENSCANNER_INVALID_SOCKET;
    std::thread accept_thread;

    std::string static_root;
    mutable std::mutex clients_mutex;
    std::vector<socket_t> clients;
    std::vector<std::thread> reader_threads;

    std::mutex handler_mutex;
    WsBroadcastServer::TextMessageHandler text_handler;

    void AcceptLoop() {
        while (running.load()) {
            sockaddr_in client_addr = {};
#if defined(_WIN32)
            int addr_len = sizeof(client_addr);
#else
            socklen_t addr_len = sizeof(client_addr);
#endif
            socket_t client = accept(listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
            if (client == OPENSCANNER_INVALID_SOCKET) {
                if (!running.load()) break;
                continue;
            }
            if (!PerformHandshake(client)) {
                OPENSCANNER_CLOSESOCK(client);
                continue;
            }
            // Backpressure: bound how long a broadcast may block on a
            // slow/stalled client. SendAll returns early on timeout, aborting
            // that frame for this client; the client then sees a truncated/
            // failed frame (and typically a closed connection), while the
            // server keeps broadcasting to the others. The reader thread only
            // prunes the client later, when it notices the dead socket.
#if defined(_WIN32)
            DWORD snd_timeout = 100;
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&snd_timeout), sizeof(snd_timeout));
#else
            struct timeval snd_timeout;
            snd_timeout.tv_sec = 0;
            snd_timeout.tv_usec = 100000;
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&snd_timeout), sizeof(snd_timeout));
#if defined(SO_NOSIGPIPE)
            // Second belt against SIGPIPE on a reset peer (macOS/BSD).
            // scan_server also ignores SIGPIPE process-wide; this covers
            // any other host of this server.
            int no_sigpipe = 1;
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&no_sigpipe), sizeof(no_sigpipe));
#endif
#endif
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.push_back(client);
                reader_threads.emplace_back([this, client]() { ClientReadLoop(client); });
            }
        }
    }

    // Serves one static file for a plain HTTP GET (the UI), then returns
    // false so the accept loop closes the one-shot connection. Only
    // reachable when SetStaticRoot() supplied a directory.
    bool ServeStaticRequest(socket_t client, const std::string& request) {
        if (static_root.empty()) return false;
        size_t line_end = request.find("\r\n");
        std::string request_line = request.substr(0, line_end == std::string::npos ? request.size() : line_end);
        if (request_line.compare(0, 4, "GET ") != 0) return false;
        std::string path = request_line.substr(4);
        size_t space = path.find(' ');
        if (space != std::string::npos) path = path.substr(0, space);
        size_t query = path.find('?');
        if (query != std::string::npos) path = path.substr(0, query);
        if (path == "/" || path.empty()) path = "/index.html";
        if (path.find("..") != std::string::npos) return false;

        std::ifstream in(static_root + path, std::ios::binary);
        if (!in) {
            const char* not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(client, reinterpret_cast<const uint8_t*>(not_found), std::strlen(not_found));
            return false;
        }
        std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const char* type = "application/octet-stream";
        auto ends_with = [&path](const char* suffix) {
            size_t n = std::strlen(suffix);
            return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
        };
        if (ends_with(".html")) type = "text/html; charset=utf-8";
        else if (ends_with(".js")) type = "text/javascript; charset=utf-8";
        else if (ends_with(".css")) type = "text/css; charset=utf-8";
        else if (ends_with(".json")) type = "application/json";
        std::string header = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(type) +
                             "\r\nContent-Length: " + std::to_string(body.size()) +
                             "\r\nConnection: close\r\n\r\n";
        SendAll(client, reinterpret_cast<const uint8_t*>(header.data()), header.size());
        if (!body.empty()) SendAll(client, reinterpret_cast<const uint8_t*>(body.data()), body.size());
        return false;  // one-shot connection; accept loop closes it
    }

    bool PerformHandshake(socket_t client) {
        char buf[4096];
#if defined(_WIN32)
        int n = recv(client, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) return false;
        std::string request(buf, static_cast<size_t>(n));

        std::string client_key;
        if (!FindHeaderValue(request, "Sec-WebSocket-Key", &client_key)) {
            return ServeStaticRequest(client, request);
        }

        std::string accept_key = ComputeAcceptKey(client_key);
        std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: " +
                                accept_key + "\r\n\r\n";
        SendAll(client, reinterpret_cast<const uint8_t*>(response.data()), response.size());
        return true;
    }

    // Reads frames from `client` until it disconnects or sends something
    // this server doesn't support (see ReadFrame), then prunes it from
    // the broadcast list. Text frames (control commands) are handed to
    // the registered handler; pings get an immediate pong; everything
    // else (binary, pong, close) is just ignored/used as a disconnect
    // signal.
    void ClientReadLoop(socket_t client) {
        uint8_t opcode = 0;
        std::vector<uint8_t> payload;
        while (running.load()) {
            if (!ReadFrame(client, &opcode, &payload)) break;
            if (opcode == 0x8 /* close */) {
                // Echo the close frame back before dropping the socket —
                // without this, well-behaved clients see an abnormal
                // closure instead of a clean one.
                SendFrame(client, 0x8, payload.data(), payload.size());
                break;
            }
            if (opcode == 0x9 /* ping */) {
                SendFrame(client, 0xA /* pong */, payload.data(), payload.size());
                continue;
            }
            if (opcode == 0x1 /* text */) {
                WsBroadcastServer::TextMessageHandler handler_copy;
                {
                    std::lock_guard<std::mutex> lock(handler_mutex);
                    handler_copy = text_handler;
                }
                if (handler_copy) {
                    handler_copy(std::string(payload.begin(), payload.end()));
                }
            }
        }
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
        OPENSCANNER_CLOSESOCK(client);
    }
};

WsBroadcastServer::WsBroadcastServer() : impl_(std::make_unique<Impl>()) {}

WsBroadcastServer::~WsBroadcastServer() { Stop(); }

void WsBroadcastServer::SetStaticRoot(const std::string& dir) { impl_->static_root = dir; }

void WsBroadcastServer::SetTextMessageHandler(TextMessageHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->handler_mutex);
    impl_->text_handler = std::move(handler);
}

bool WsBroadcastServer::Start(uint16_t port) {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
#endif

    impl_->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listen_socket == OPENSCANNER_INVALID_SOCKET) return false;

    int reuse = 1;
    setsockopt(impl_->listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(impl_->listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
        return false;
    }
    if (listen(impl_->listen_socket, 8) != 0) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
        return false;
    }

    impl_->running = true;
    impl_->accept_thread = std::thread([this]() { impl_->AcceptLoop(); });
    return true;
}

void WsBroadcastServer::Stop() {
    if (!impl_->running.exchange(false)) return;

    if (impl_->listen_socket != OPENSCANNER_INVALID_SOCKET) {
        OPENSCANNER_CLOSESOCK(impl_->listen_socket);
        impl_->listen_socket = OPENSCANNER_INVALID_SOCKET;
    }
    if (impl_->accept_thread.joinable()) impl_->accept_thread.join();

    std::vector<socket_t> clients_copy;
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        clients_copy = impl_->clients;
    }
    for (socket_t client : clients_copy) {
        OPENSCANNER_CLOSESOCK(client);
    }
    for (auto& t : impl_->reader_threads) {
        if (t.joinable()) t.join();
    }
    impl_->reader_threads.clear();
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        impl_->clients.clear();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
}

void WsBroadcastServer::BroadcastBinary(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    for (auto it = impl_->clients.begin(); it != impl_->clients.end();) {
        if (!SendFrame(*it, 0x2 /* binary */, data, size)) {
            // A partial frame already went out; never write another frame to
            // this socket. Drop it from the broadcast list and wake its
            // reader, which owns the actual close().
            socket_t dead = *it;
            it = impl_->clients.erase(it);
            OPENSCANNER_SHUTDOWN(dead);
        } else {
            ++it;
        }
    }
}

void WsBroadcastServer::BroadcastText(const std::string& text) {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    for (auto it = impl_->clients.begin(); it != impl_->clients.end();) {
        if (!SendFrame(*it, 0x1 /* text */, reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
            socket_t dead = *it;
            it = impl_->clients.erase(it);
            OPENSCANNER_SHUTDOWN(dead);
        } else {
            ++it;
        }
    }
}

size_t WsBroadcastServer::ConnectedClientCount() const {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex);
    return impl_->clients.size();
}

}  // namespace openscanner
