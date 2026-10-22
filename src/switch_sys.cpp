#include "stremio/switch_sys.hpp"
#include <iostream>

#ifdef __SWITCH__
#include <switch.h>

namespace Stremio {

static bool s_sysInitialized = false;
static uint32_t s_stockCpu = 0;
static uint32_t s_stockGpu = 0;
static uint32_t s_stockEmc = 0;

static uint32_t getClockRate(PcvModuleId id) {
    uint32_t hz = 0;
    ClkrstSession session = {0};
    if (R_SUCCEEDED(clkrstOpenSession(&session, id, 3))) {
        clkrstGetClockRate(&session, &hz);
        clkrstCloseSession(&session);
    }
    return hz;
}

static void setClockRate(PcvModuleId id, uint32_t hz) {
    ClkrstSession session = {0};
    if (R_SUCCEEDED(clkrstOpenSession(&session, id, 3))) {
        clkrstSetClockRate(&session, hz);
        clkrstCloseSession(&session);
    }
}

void SwitchSys::init() {
    if (s_sysInitialized) return;
    s_stockCpu = getClockRate(PcvModuleId_CpuBus);
    s_stockGpu = getClockRate(PcvModuleId_GPU);
    s_stockEmc = getClockRate(PcvModuleId_EMC);
    s_sysInitialized = true;
    std::cout << "[SwitchSys] Stock Clocks: CPU=" << s_stockCpu / 1000000 
              << "MHz GPU=" << s_stockGpu / 1000000 
              << "MHz EMC=" << s_stockEmc / 1000000 << "MHz" << std::endl;
}

void SwitchSys::boostPlayback() {
    init();
    std::cout << "[SwitchSys] Boosting CPU to 1785 MHz and EMC to 1600 MHz for smooth playback..." << std::endl;
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    setClockRate(PcvModuleId_CpuBus, 1785000000); // 1.785 GHz CPU Max
    setClockRate(PcvModuleId_EMC, 1600000000);    // 1.600 GHz RAM Max bandwidth
    setClockRate(PcvModuleId_GPU, 768000000);     // 768 MHz GPU
}

void SwitchSys::restoreStock() {
    std::cout << "[SwitchSys] Restoring stock clocks..." << std::endl;
    appletCancelCpuBoostMode();
    if (s_stockCpu > 0) setClockRate(PcvModuleId_CpuBus, s_stockCpu);
    if (s_stockGpu > 0) setClockRate(PcvModuleId_GPU, s_stockGpu);
    if (s_stockEmc > 0) setClockRate(PcvModuleId_EMC, s_stockEmc);
}

} // namespace Stremio

#else

namespace Stremio {
void SwitchSys::init() {}
void SwitchSys::boostPlayback() {}
void SwitchSys::restoreStock() {}
} // namespace Stremio

#endif
