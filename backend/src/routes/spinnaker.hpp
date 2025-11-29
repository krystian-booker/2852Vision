#pragma once

#include <drogon/drogon.h>

namespace vision {

class SpinnakerController {
public:
    static void registerRoutes(drogon::HttpAppFramework& app);
};

} // namespace vision
