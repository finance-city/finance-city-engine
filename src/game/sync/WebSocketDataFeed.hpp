#pragma once

#include "PriceUpdate.hpp"
#include <string>

#ifndef __EMSCRIPTEN__
#include <ixwebsocket/IXWebSocket.h>
#include <mutex>
#else
#include <emscripten/websocket.h>
#endif

/**
 * @brief Real-time price feed via WebSocket
 *
 * Native: uses IXWebSocket (background thread, mutex-protected).
 * WASM:   uses Emscripten WebSocket API (single-threaded, callback-driven).
 *
 * Both builds expose the same interface:
 *   connect / disconnect / isConnected / drainUpdates
 */
class WebSocketDataFeed {
public:
    WebSocketDataFeed();
    ~WebSocketDataFeed();

    // Disable copy/move
    WebSocketDataFeed(const WebSocketDataFeed&) = delete;
    WebSocketDataFeed& operator=(const WebSocketDataFeed&) = delete;

    void connect(const std::string& url);
    void disconnect();

    bool isConnected() const;

    // Call once per frame from the main thread.
    // Returns all updates accumulated since the last call, then clears the buffer.
    PriceUpdateBatch drainUpdates();

private:
    // Shared JSON parsing (used by both native and WASM callbacks)
    void parseAndEnqueue(const std::string& raw);
    bool firstMsg_ = true;  // tracks whether the first message has been logged

#ifndef __EMSCRIPTEN__
    // ---- Native (IXWebSocket) ----
    void onMessage(const ix::WebSocketMessagePtr& msg);

    ix::WebSocket      ws_;
    mutable std::mutex mutex_;
    PriceUpdateBatch   pending_;
#else
    // ---- WASM (Emscripten WebSocket API) ----
    EMSCRIPTEN_WEBSOCKET_T ws_        = 0;
    bool                   connected_ = false;
    PriceUpdateBatch       pending_;          // main-thread only; no mutex needed

    static EM_BOOL onOpenCb   (int, const EmscriptenWebSocketOpenEvent*,    void*);
    static EM_BOOL onCloseCb  (int, const EmscriptenWebSocketCloseEvent*,   void*);
    static EM_BOOL onMessageCb(int, const EmscriptenWebSocketMessageEvent*, void*);
    static EM_BOOL onErrorCb  (int, const EmscriptenWebSocketErrorEvent*,   void*);
#endif
};
