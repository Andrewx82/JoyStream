#include "stremio/ui_engine.hpp"
#include "stremio/http_client.hpp"
#include <iostream>

#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char* argv[]) {
#ifdef __SWITCH__
    // Initialize Nintendo Switch system services
    appletLockExit(); // Graceful homebrew exit handling
#endif

    std::cout << "Starting JoyStream 0.1..." << std::endl;

    if (!Stremio::HttpClient::initializeNetwork()) {
        std::cerr << "Failed to initialize network sockets!" << std::endl;
        return 1;
    }

    {
        Stremio::UiEngine app;
        if (app.initialize()) {
            app.run();
        }
        app.shutdown();
    }

    Stremio::HttpClient::cleanupNetwork();

#ifdef __SWITCH__
    appletUnlockExit();
#endif

    std::cout << "JoyStream exited cleanly." << std::endl;
    return 0;
}
