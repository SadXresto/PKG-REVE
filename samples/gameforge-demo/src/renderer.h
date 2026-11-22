// Minimal renderer declaration for the NeonDrift demo project.
#pragma once

struct SDL_Window;

namespace neondrift {

class Renderer {
public:
    void initialize(SDL_Window* window);
    void drawFrame();
    void shutdown();

private:
    SDL_Window* m_window = nullptr;
};

} // namespace neondrift
