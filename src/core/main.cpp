#include "Game.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Game game;
    if (!game.init()) {
        return 1;
    }
    game.run();
    return 0;
}

#else

int main(int argc, char* argv[]) {
    Game game;
    if (!game.init()) {
        return 1;
    }
    game.run();
    return 0;
}

#endif
