#pragma once

#include <drogon/drogon.h>

namespace vision {

class SettingsController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
