#pragma once

#include <crow.h>

namespace vision {

class NetworkTablesRoutes {
public:
    // Register NetworkTables API routes
    static void registerRoutes(crow::SimpleApp& app);
};

} // namespace vision
