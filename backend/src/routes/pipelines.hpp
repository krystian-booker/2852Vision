#pragma once

#include <drogon/drogon.h>

namespace vision {

class PipelinesController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
