#pragma once

#include <cstdint>

namespace Stremio {

class SwitchSys {
public:
    static void init();
    static void boostPlayback();
    static void restoreStock();
};

} // namespace Stremio
