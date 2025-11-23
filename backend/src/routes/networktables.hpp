#pragma once

#include <drogon/drogon.h>

namespace vision {

class NetworkTablesRoutes {
public:
    // Register NetworkTables API routes
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
