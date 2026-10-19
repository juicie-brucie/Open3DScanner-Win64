#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openscanner {

// A minimal, dependency-free WebSocket server for two purposes: broadcast
// binary frames (point-cloud data) and text frames (status/control) to
// whichever UI clients are connected, as fast and simply as possible.
// Deliberately not a general WebSocket implementation — no TLS, no
// fragmentation (incoming frames must be a single, unfragmented frame;
// every real control message is small enough that no browser fragments
// it). It listens on all interfaces (not just loopback) so the UI can be
// reached from another machine on the LAN; there is still no auth, so
// don't run it on an untrusted network.
class WsBroadcastServer {
public:
    // Called from an internal background thread (one per connected
    // client) whenever that client sends a text frame. Must not block
    // and must not call back into this server synchronously (Broadcast*
    // takes a lock the caller may already be inside of, indirectly).
    using TextMessageHandler = std::function<void(const std::string&)>;

    WsBroadcastServer();
    ~WsBroadcastServer();

    WsBroadcastServer(const WsBroadcastServer&) = delete;
    WsBroadcastServer& operator=(const WsBroadcastServer&) = delete;

    // Registers the handler for incoming client text frames (e.g. JSON
    // control commands). Call before Start(); not thread-safe against a
    // concurrent Start().
    void SetTextMessageHandler(TextMessageHandler handler);

    // Serves the UI from `dir` on the same port (a non-WebSocket request
    // gets a static file instead of the 101 handshake). Empty disables it.
    // Call before Start(); not thread-safe against a concurrent Start().
    void SetStaticRoot(const std::string& dir);

    // Starts listening on 0.0.0.0:port (all interfaces) and accepting
    // client connections on a background thread. Returns false if the
    // socket couldn't be bound (port already in use, etc.).
    bool Start(uint16_t port);

    // Stops accepting new connections, closes all client sockets, and
    // joins background threads. Safe to call even if Start() failed.
    void Stop();

    // Sends `data` (raw bytes, becomes one WebSocket binary frame) to
    // every currently-connected client. Thread-safe; cheap no-op if no
    // clients are connected.
    void BroadcastBinary(const uint8_t* data, size_t size);

    // Sends `text` as one WebSocket text frame to every currently-
    // connected client (e.g. a status update). Thread-safe; cheap no-op
    // if no clients are connected.
    void BroadcastText(const std::string& text);

    // Number of currently-connected clients. Thread-safe.
    size_t ConnectedClientCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace openscanner
