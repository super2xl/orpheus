#include "installer_app.h"
#include <iostream>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    orpheus::installer::InstallerApp app;

    if (!app.Initialize()) {
        std::cerr << "Failed to initialize MCPinstaller" << std::endl;
        return 1;
    }

    return app.Run();
}
