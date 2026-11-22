// NeonDrift demo entry point. Synthetic code for GameForge demonstrations.
#include "renderer.h"

#include <SDL.h>

#include <thread>

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return 1;

    SDL_Window* window = SDL_CreateWindow("NeonDrift", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 1280, 720,
                                          SDL_WINDOW_OPENGL);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }

    neondrift::Renderer renderer;
    renderer.initialize(window);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT)
                running = false;
        }
        renderer.drawFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
