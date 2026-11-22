// Desktop OpenGL renderer for the NeonDrift demo. Synthetic code only.
#include "renderer.h"

#include <SDL.h>
#include <SDL_opengl.h>

namespace neondrift {

void Renderer::initialize(SDL_Window* window)
{
    m_window = window;
    glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawFrame()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.5f, -0.5f);
    glVertex2f(0.5f, -0.5f);
    glVertex2f(0.0f, 0.5f);
    glEnd();
    SDL_GL_SwapWindow(m_window);
}

void Renderer::shutdown()
{
    m_window = nullptr;
}

} // namespace neondrift
