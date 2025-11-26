#pragma once

#include <drogon/drogon.h>

namespace vision {

class CamerasController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
