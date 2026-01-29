#include "bh_app.h"

#include "core/bh_parse.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// Graphics backend selection
// -----------------------------------------------------------------------------
// Toggle the default GPU backend in this one place.
//
// 1 = OpenGL backend (desktop)
// 0 = SDL_gpu backend
#ifndef BH_USE_OPENGL_BACKEND
#define BH_USE_OPENGL_BACKEND 0
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

static BH_App g_app;

// Read an integer URL query parameter in web builds.
// Example: index.html?fps=144&vsync=1
static int bh_web_query_int(const char *key, int default_value)
{
    if (!key || !key[0])
        return default_value;

    // Note: key is a constant string from our code, not user-provided.
    char script[512];
    (void)snprintf(script, sizeof(script),
                  "(() => {"
                  "  try {"
                  "    const v = (new URLSearchParams(window.location.search)).get('%s');"
                  "    if (v === null) return %d;"
                  "    const n = parseInt(v, 10);"
                  "    return Number.isFinite(n) ? n : %d;"
                  "  } catch (e) { return %d; }"
                  "})()",
                  key, default_value, default_value, default_value);

    return emscripten_run_script_int(script);
}

static void bh_emscripten_mainloop(void *userdata)
{
    BH_App *app = (BH_App *)userdata;

    if (!BH_App_Tick(app))
    {
        BH_App_Shutdown(app);
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;

    // Web builds use SDL_gpu.
    BH_GPU_SetBackend(&BH_GPU_BACKEND_GL);

    /* Ensure SDL uses the canvas element provided by our shell HTML. */
    (void)SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, "#canvas");

    BH_AppConfig cfg = {0};
    cfg.num_windows = 1;
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.fixed_dt = 1.0 / 100.0; // tickrate
    cfg.debug_gpu = false;
    cfg.enable_vsync = (bh_web_query_int("vsync", 1) != 0);
    cfg.target_fps = bh_web_query_int("fps", 0);

    if (!BH_App_Init(&g_app, &cfg))
    {
        SDL_Log("[bh] App init failed");
        return 1;
    }

    /*
        Run the app through Emscripten's browser mainloop.

        - fps=0   (default): requestAnimationFrame cadence (vsync)
        - fps>0              : setTimeout cadence (attempt higher-than-60Hz)
        - fps<0              : run as fast as possible (CPU heavy; still presents at vsync)
    */
    const int fps_override = cfg.target_fps;
    const int loop_fps = (fps_override > 0) ? fps_override : 0;
    emscripten_set_main_loop_arg(bh_emscripten_mainloop, &g_app, loop_fps, 1);

    if (fps_override < 0)
    {
        emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
    }
    return 0;
#else
    (void)argc;

#if BH_USE_OPENGL_BACKEND
    BH_GPU_SetBackend(&BH_GPU_BACKEND_GL);
#else
    BH_GPU_SetBackend(&BH_GPU_BACKEND_SDL);
#endif

    BH_AppConfig cfg = {0};
    cfg.num_windows = (argc >= 2) ? (int)BH_Parse_Int32Clamp(argv[1], 1, 1, 8) : 1;
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.fixed_dt = 1.0 / 100.0; // tickrate
    cfg.debug_gpu = false;

    BH_App app;
    if (!BH_App_Init(&app, &cfg))
    {
        SDL_Log("[bh] App init failed");
        return 1;
    }

    const int code = BH_App_Run(&app);
    BH_App_Shutdown(&app);
    return code;
#endif
}
