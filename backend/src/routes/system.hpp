#pragma once

#include <drogon/drogon.h>

namespace vision {

class SystemController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
