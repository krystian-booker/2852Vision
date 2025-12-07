// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/vision_ws.hpp"
#include "services/networktables_service.hpp"
#include "metrics/registry.hpp"
#include <spdlog/spdlog.h>

namespace vision {

VisionWebSocket& VisionWebSocket::instance() {
    static VisionWebSocket inst;
    return inst;
}

void VisionWebSocket::handleNewConnection(
    const drogon::HttpRequestPtr& req,
    const drogon::WebSocketConnectionPtr& conn) {

    spdlog::info("VisionWebSocket: Client connected from {}",
                 conn->peerAddr().toIpPort());

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_[conn] = ClientSubscriptions{};
    }
}

void VisionWebSocket::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr& conn) {

    spdlog::info("VisionWebSocket: Client disconnected");

    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(conn);
}

void VisionWebSocket::handleNewMessage(
    const drogon::WebSocketConnectionPtr& conn,
    std::string&& message,
    const drogon::WebSocketMessageType& type) {

    // Handle WebSocket ping/pong
    if (type == drogon::WebSocketMessageType::Ping) {
        conn->send("", drogon::WebSocketMessageType::Pong);
        return;
    }

    try {
        auto json = nlohmann::json::parse(message);
        std::string msgType = json.value("type", "");

        if (msgType == "subscribe") {
            handleSubscribe(conn, json);
        } else if (msgType == "unsubscribe") {
            handleUnsubscribe(conn, json);
        } else if (msgType == "ping") {
            nlohmann::json response = {{"type", "pong"}};
            conn->send(response.dump());
        }
    } catch (const std::exception& e) {
        spdlog::warn("VisionWebSocket: Failed to parse message: {}", e.what());
    }
}

void VisionWebSocket::handleSubscribe(
    const drogon::WebSocketConnectionPtr& conn,
    const nlohmann::json& msg) {

    std::string topic = msg.value("topic", "");

    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(conn);
    if (it == clients_.end()) return;

    auto& subs = it->second;

    if (topic == "metrics") {
        subs.metrics = true;
        spdlog::debug("VisionWebSocket: Client subscribed to metrics");

        // Send current metrics immediately
        auto summary = MetricsRegistry::instance().getSummary();
        nlohmann::json response = {
            {"type", "metrics"},
            {"data", summary.toJson()}
        };
        conn->send(response.dump());

    } else if (topic == "nt_status") {
        subs.ntStatus = true;
        spdlog::debug("VisionWebSocket: Client subscribed to nt_status");

        // Send current NT status immediately
        auto status = NetworkTablesService::instance().getStatus();
        nlohmann::json response = {
            {"type", "nt_status"},
            {"data", status.toJson()}
        };
        conn->send(response.dump());

    } else if (topic == "camera_status") {
        int cameraId = msg.value("cameraId", -1);
        if (cameraId >= 0) {
            subs.cameraStatus.insert(cameraId);
            spdlog::debug("VisionWebSocket: Client subscribed to camera_status for camera {}", cameraId);
        }

    } else if (topic == "pipeline_results") {
        int cameraId = msg.value("cameraId", -1);
        int pipelineId = msg.value("pipelineId", -1);
        if (cameraId >= 0 && pipelineId >= 0) {
            subs.pipelineResults.insert({cameraId, pipelineId});
            spdlog::debug("VisionWebSocket: Client subscribed to pipeline_results for camera {} pipeline {}",
                          cameraId, pipelineId);
        }
    }
}

void VisionWebSocket::handleUnsubscribe(
    const drogon::WebSocketConnectionPtr& conn,
    const nlohmann::json& msg) {

    std::string topic = msg.value("topic", "");

    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(conn);
    if (it == clients_.end()) return;

    auto& subs = it->second;

    if (topic == "metrics") {
        subs.metrics = false;
    } else if (topic == "nt_status") {
        subs.ntStatus = false;
    } else if (topic == "camera_status") {
        int cameraId = msg.value("cameraId", -1);
        if (cameraId >= 0) {
            subs.cameraStatus.erase(cameraId);
        }
    } else if (topic == "pipeline_results") {
        int cameraId = msg.value("cameraId", -1);
        int pipelineId = msg.value("pipelineId", -1);
        if (cameraId >= 0 && pipelineId >= 0) {
            subs.pipelineResults.erase({cameraId, pipelineId});
        }
    }
}

void VisionWebSocket::sendToClient(
    const drogon::WebSocketConnectionPtr& conn,
    const nlohmann::json& msg) {

    if (conn->connected()) {
        conn->send(msg.dump());
    }
}

void VisionWebSocket::broadcastMetrics(const nlohmann::json& metrics) {
    nlohmann::json msg = {
        {"type", "metrics"},
        {"data", metrics}
    };
    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.metrics && conn->connected()) {
            conn->send(payload);
        }
    }
}

void VisionWebSocket::broadcastCameraStatus(int cameraId, bool connected, bool streaming) {
    nlohmann::json msg = {
        {"type", "camera_status"},
        {"cameraId", cameraId},
        {"data", {
            {"camera_id", cameraId},
            {"connected", connected},
            {"streaming", streaming}
        }}
    };
    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.cameraStatus.count(cameraId) > 0 && conn->connected()) {
            conn->send(payload);
        }
    }
}

void VisionWebSocket::broadcastPipelineResults(int cameraId, int pipelineId, const nlohmann::json& results) {
    nlohmann::json msg = {
        {"type", "pipeline_results"},
        {"cameraId", cameraId},
        {"pipelineId", pipelineId},
        {"data", results}
    };
    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.pipelineResults.count({cameraId, pipelineId}) > 0 && conn->connected()) {
            conn->send(payload);
        }
    }
}

void VisionWebSocket::broadcastNTStatus(const nlohmann::json& status) {
    nlohmann::json msg = {
        {"type", "nt_status"},
        {"data", status}
    };
    std::string payload = msg.dump();

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.ntStatus && conn->connected()) {
            conn->send(payload);
        }
    }
}

bool VisionWebSocket::hasMetricsSubscribers() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.metrics) return true;
    }
    return false;
}

bool VisionWebSocket::hasCameraStatusSubscribers(int cameraId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.cameraStatus.count(cameraId) > 0) return true;
    }
    return false;
}

bool VisionWebSocket::hasPipelineResultsSubscribers(int cameraId, int pipelineId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.pipelineResults.count({cameraId, pipelineId}) > 0) return true;
    }
    return false;
}

bool VisionWebSocket::hasNTStatusSubscribers() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (const auto& [conn, subs] : clients_) {
        if (subs.ntStatus) return true;
    }
    return false;
}

size_t VisionWebSocket::getClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

void VisionWebSocket::startMetricsBroadcast() {
    if (metricsRunning_.load()) return;

    metricsRunning_.store(true);
    metricsThread_ = std::thread([this]() {
        metricsBroadcastLoop();
    });

    spdlog::info("VisionWebSocket: Metrics broadcast started");
}

void VisionWebSocket::stopMetricsBroadcast() {
    metricsRunning_.store(false);
    if (metricsThread_.joinable()) {
        metricsThread_.join();
    }
    spdlog::info("VisionWebSocket: Metrics broadcast stopped");
}

void VisionWebSocket::metricsBroadcastLoop() {
    while (metricsRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!metricsRunning_.load()) break;

        // Only broadcast if there are subscribers
        if (hasMetricsSubscribers()) {
            auto summary = MetricsRegistry::instance().getSummary();
            broadcastMetrics(summary.toJson());
        }
    }
}

} // namespace vision
