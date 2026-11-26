#ifndef KDC_GL_FIXTURE_H
#define KDC_GL_FIXTURE_H

#include "gl_core_3_1.h"
#include "desktop/sdl_compat.h"
#include <assert.h>

static SDL_Window *gpu_window;
static SDL_GLContext gpu_context;

static void SetGlAttribute(SDL_GLAttr attribute, int value)
{
#if SNESRECOMP_SDL3
    assert(SDL_GL_SetAttribute(attribute, value));
#else
    assert(SDL_GL_SetAttribute(attribute, value) == 0);
#endif
}

static void OpenGlFixture(int major, int minor)
{
    assert(snesrecomp_sdl_init(SDL_INIT_VIDEO));
    SetGlAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SetGlAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SetGlAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    gpu_window = snesrecomp_sdl_create_window("Renderer differential fixture", 256, 224,
                                             SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    assert(gpu_window);
    gpu_context = SDL_GL_CreateContext(gpu_window);
    assert(gpu_context);
    ogl_LoadFunctions();
    assert(ogl_IsVersionGEQ(major, minor));
}

static void CloseGlFixture(void)
{
#if SNESRECOMP_SDL3
    SDL_GL_DestroyContext(gpu_context);
#else
    SDL_GL_DeleteContext(gpu_context);
#endif
    SDL_DestroyWindow(gpu_window);
    SDL_Quit();
}

#endif
