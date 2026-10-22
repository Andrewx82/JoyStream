#include "stremio/ui_engine.hpp"
#include "stremio/http_client.hpp"
#include "stremio/switch_sys.hpp"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

void setupFileLogging() {
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/joystream", 0777);

    FILE* logFp = freopen("sdmc:/switch/joystream/joystream.log", "w", stdout);
    if (logFp) {
        dup2(fileno(logFp), fileno(stderr));
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
    std::cout << "========================================" << std::endl;
    std::cout << "  JoyStream v0.1 (Nintendo Switch)     " << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    setupFileLogging();

#ifdef __SWITCH__
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);
    appletLockExit();
    Stremio::SwitchSys::init();
#endif

    std::cout << "[Init] Starting network subsystem..." << std::endl;
    bool networkOk = Stremio::HttpClient::initializeNetwork();
    if (!networkOk) {
        std::cerr << "[Warning] Network initialization failed. JoyStream will start in offline mode." << std::endl;
    } else {
        std::cout << "[Init] Network subsystem initialized successfully." << std::endl;
    }

    try {
        std::cout << "[Init] Initializing UI Engine..." << std::endl;
        Stremio::UiEngine app;
        if (app.initialize()) {
            std::cout << "[Init] UI Engine initialized. Entering main loop." << std::endl;
            app.run();
        } else {
            std::cerr << "[Error] UI Engine failed to initialize!" << std::endl;
        }
        std::cout << "[Exit] Shutting down UI Engine..." << std::endl;
        app.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Uncaught exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[FATAL] Unknown uncaught exception occurred!" << std::endl;
    }

    if (networkOk) {
        std::cout << "[Exit] Cleaning up network..." << std::endl;
        Stremio::HttpClient::cleanupNetwork();
    }

#ifdef __SWITCH__
    Stremio::SwitchSys::restoreStock();
    appletUnlockExit();
#endif

    std::cout << "[Exit] JoyStream exited cleanly." << std::endl;
    return 0;
}
