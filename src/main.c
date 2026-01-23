#include "bh_app.h"

#include "core/bh_parse.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    (void)argc;

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
}
