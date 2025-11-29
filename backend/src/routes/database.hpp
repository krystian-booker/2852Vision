#pragma once

#include <drogon/drogon.h>
#include <string>

namespace vision {

class DatabaseController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app, const std::string& databasePath);
};

} // namespace vision
