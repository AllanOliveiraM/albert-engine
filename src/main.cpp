#include "render/application.hpp"

#ifdef _WIN32
#include <cstdlib>
#include <windows.h>

// GUI subsystem: opening the portable executable does not create a console.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return albert::runRenderer(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return albert::runRenderer(argc, argv);
}
#endif
