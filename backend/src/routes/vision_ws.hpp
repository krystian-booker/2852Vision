#pragma once

#include <drogon/WebSocketController.h>
#include <set>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

namespace vision {

// Client subscription state
struct ClientSubscriptions {
    bool metrics = false;
    bool ntStatus = false;
    std::set<int> cameraStatus;                          // camera IDs
    std::set<std::pair<int, int>> pipelineResults;       // (cameraId, pipelineId) pairs
};

class VisionWebSocket : public drogon::WebSocketController<VisionWebSocket> {
public:
    // Singleton for broadcasting from other parts of the application
    static VisionWebSocket& instance();

    // WebSocket lifecycle methods
    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;

    void handleNewConnection(const drogon::HttpRequestPtr& req,
                             const drogon::WebSocketConnectionPtr& conn) override;

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;

    // Broadcast methods - called by services when data changes
    void broadcastMetrics(const nlohmann::json& metrics);
    void broadcastCameraStatus(int cameraId, bool connected, bool streaming);
    void broadcastPipelineResults(int cameraId, int pipelineId, const nlohmann::json& results);
    void broadcastNTStatus(const nlohmann::json& status);

    // Check if any client is subscribed to a topic
    bool hasMetricsSubscribers() const;
    bool hasCameraStatusSubscribers(int cameraId) const;
    bool hasPipelineResultsSubscribers(int cameraId, int pipelineId) const;
    bool hasNTStatusSubscribers() const;

    // Get client count for debugging
    size_t getClientCount() const;

    // Start/stop metrics broadcast thread
    void startMetricsBroadcast();
    void stopMetricsBroadcast();

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/vision");
    WS_PATH_LIST_END

private:
    void handleSubscribe(const drogon::WebSocketConnectionPtr& conn, const nlohmann::json& msg);
    void handleUnsubscribe(const drogon::WebSocketConnectionPtr& conn, const nlohmann::json& msg);
    void sendToClient(const drogon::WebSocketConnectionPtr& conn, const nlohmann::json& msg);

    // Metrics broadcast thread
    void metricsBroadcastLoop();

    static std::mutex clientsMutex_;
    static std::map<drogon::WebSocketConnectionPtr, ClientSubscriptions> clients_;

    // Metrics broadcast thread
    std::thread metricsThread_;
    std::atomic<bool> metricsRunning_{false};
};

} // namespace vision
