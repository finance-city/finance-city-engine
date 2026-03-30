#include "WebSocketDataFeed.hpp"
#include "src/utils/Logger.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// =============================================================================
// Shared JSON parsing (native + WASM)
// =============================================================================

void WebSocketDataFeed::parseAndEnqueue(const std::string& raw) {
    // Log the first message received to help diagnose format mismatches
    static bool firstMsg = true;
    if (firstMsg) {
        firstMsg = false;
        std::string preview = raw.size() > 300 ? raw.substr(0, 300) + "..." : raw;
        LOG_INFO("WebSocketDataFeed") << "First message received: " << preview;
    }

    try {
        auto j = json::parse(raw);

        // Accept both "batch_stock_data" and bare array payloads
        const std::string msgType = j.value("type", "");

        // If the message is itself an array, treat it as a bare payload
        if (j.is_array()) {
            PriceUpdateBatch batch;
            batch.reserve(j.size());
            for (const auto& item : j) {
                PriceUpdate u;
                u.ticker    = item.value("c",  item.value("ticker", item.value("symbol", "")));
                u.price     = item.value("p",  item.value("price",  0.0f));
                u.rate      = item.value("r",  item.value("rate",   item.value("change_pct", 0.0f)));
                u.volume    = item.value("vt", item.value("volume", 0.0f));
                u.timestamp = item.value("t",  item.value("ts",     (uint64_t)0));
                if (u.ticker.empty() || u.price == 0.0f) continue;
                batch.push_back(std::move(u));
            }
            if (!batch.empty()) {
#ifndef __EMSCRIPTEN__
                std::lock_guard<std::mutex> lock(mutex_);
#endif
                pending_.insert(pending_.end(), batch.begin(), batch.end());
            }
            return;
        }

        if (msgType != "batch_stock_data") {
            LOG_DEBUG("WebSocketDataFeed") << "Ignoring message type: '"
                                           << msgType << "'";
            return;
        }

        // Support both "payload" and "data" as the array key
        const auto& payload = j.contains("payload") ? j["payload"] : j["data"];
        if (!payload.is_array() || payload.empty()) return;

        PriceUpdateBatch batch;
        batch.reserve(payload.size());

        for (const auto& item : payload) {
            PriceUpdate u;
            // Field name fallbacks: compact → verbose
            u.ticker    = item.value("c",  item.value("ticker",     item.value("symbol",     "")));
            u.price     = item.value("p",  item.value("price",      0.0f));
            u.rate      = item.value("r",  item.value("rate",       item.value("change_pct", 0.0f)));
            u.volume    = item.value("vt", item.value("volume",     0.0f));
            u.timestamp = item.value("t",  item.value("ts",         (uint64_t)0));
            if (u.ticker.empty() || u.price == 0.0f) continue;
            batch.push_back(std::move(u));
        }

        {
#ifndef __EMSCRIPTEN__
            std::lock_guard<std::mutex> lock(mutex_);
#endif
            pending_.insert(pending_.end(), batch.begin(), batch.end());
        }

    } catch (const std::exception& e) {
        LOG_ERROR("WebSocketDataFeed") << "Parse error: " << e.what();
    }
}

// =============================================================================
// Native implementation (IXWebSocket)
// =============================================================================
#ifndef __EMSCRIPTEN__

WebSocketDataFeed::WebSocketDataFeed() {
    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        onMessage(msg);
    });
    ws_.enableAutomaticReconnection();
    ws_.setReconnectDelay(2000);
}

WebSocketDataFeed::~WebSocketDataFeed() {
    disconnect();
}

void WebSocketDataFeed::connect(const std::string& url) {
    ws_.setUrl(url);
    ws_.start();
    LOG_INFO("WebSocketDataFeed") << "Connecting to " << url;
}

void WebSocketDataFeed::disconnect() {
    ws_.stop();
}

bool WebSocketDataFeed::isConnected() const {
    return ws_.getReadyState() == ix::ReadyState::Open;
}

PriceUpdateBatch WebSocketDataFeed::drainUpdates() {
    std::lock_guard<std::mutex> lock(mutex_);
    PriceUpdateBatch out;
    std::swap(out, pending_);
    return out;
}

void WebSocketDataFeed::onMessage(const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            LOG_INFO("WebSocketDataFeed") << "Connected";
            break;
        case ix::WebSocketMessageType::Close:
            LOG_INFO("WebSocketDataFeed") << "Disconnected: " << msg->closeInfo.reason;
            break;
        case ix::WebSocketMessageType::Error:
            LOG_ERROR("WebSocketDataFeed") << "Error: " << msg->errorInfo.reason;
            break;
        case ix::WebSocketMessageType::Message:
            parseAndEnqueue(msg->str);
            break;
        default:
            break;
    }
}

#else
// =============================================================================
// WASM implementation (Emscripten WebSocket API)
// =============================================================================

WebSocketDataFeed::WebSocketDataFeed() : ws_(0), connected_(false) {}

WebSocketDataFeed::~WebSocketDataFeed() {
    disconnect();
}

void WebSocketDataFeed::connect(const std::string& url) {
    if (ws_ > 0) disconnect();

    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes(&attrs);
    attrs.url = url.c_str();

    ws_ = emscripten_websocket_new(&attrs);
    if (ws_ <= 0) {
        LOG_ERROR("WebSocketDataFeed") << "emscripten_websocket_new failed (err=" << ws_ << ")";
        ws_ = 0;
        return;
    }

    emscripten_websocket_set_onopen_callback   (ws_, this, onOpenCb);
    emscripten_websocket_set_onclose_callback  (ws_, this, onCloseCb);
    emscripten_websocket_set_onmessage_callback(ws_, this, onMessageCb);
    emscripten_websocket_set_onerror_callback  (ws_, this, onErrorCb);

    LOG_INFO("WebSocketDataFeed") << "Connecting to " << url;
}

void WebSocketDataFeed::disconnect() {
    if (ws_ > 0) {
        emscripten_websocket_close(ws_, 1000, "Client disconnect");
        emscripten_websocket_delete(ws_);
        ws_        = 0;
        connected_ = false;
    }
}

bool WebSocketDataFeed::isConnected() const {
    return connected_;
}

PriceUpdateBatch WebSocketDataFeed::drainUpdates() {
    PriceUpdateBatch out;
    std::swap(out, pending_);
    return out;
}

EM_BOOL WebSocketDataFeed::onOpenCb(int, const EmscriptenWebSocketOpenEvent*, void* ud) {
    auto* self = static_cast<WebSocketDataFeed*>(ud);
    self->connected_ = true;
    LOG_INFO("WebSocketDataFeed") << "Connected";
    return EM_TRUE;
}

EM_BOOL WebSocketDataFeed::onCloseCb(int, const EmscriptenWebSocketCloseEvent*, void* ud) {
    auto* self = static_cast<WebSocketDataFeed*>(ud);
    self->connected_ = false;
    LOG_INFO("WebSocketDataFeed") << "Disconnected";
    return EM_TRUE;
}

EM_BOOL WebSocketDataFeed::onMessageCb(int, const EmscriptenWebSocketMessageEvent* e, void* ud) {
    if (!e->isText) return EM_TRUE;
    auto* self = static_cast<WebSocketDataFeed*>(ud);
    self->parseAndEnqueue(std::string(reinterpret_cast<const char*>(e->data), e->numBytes));
    return EM_TRUE;
}

EM_BOOL WebSocketDataFeed::onErrorCb(int, const EmscriptenWebSocketErrorEvent*, void* ud) {
    LOG_ERROR("WebSocketDataFeed") << "WebSocket error";
    return EM_TRUE;
}

#endif // __EMSCRIPTEN__
