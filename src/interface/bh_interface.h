#pragma once

#include "core/bh_core.h"

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
      bh_interface

      Higher-level UI system built on top of the render-focused bh_ui module.

      Responsibilities:
        - Create/manage UI views (documents/screens) in a modular way.
        - Drive the RmlUi context update per frame.
        - Route SDL input events into RmlUi.
        - Provide simple capture/focus queries for the game.

      Notes:
        - bh_ui owns the RmlUi context + rendering integration.
        - bh_interface owns view instances + document lifetimes and input routing.
    */

    typedef struct BH_UI BH_UI;
    typedef struct BH_Interface BH_Interface;

    typedef void (*BH_UIActionFn)(void *user, const char *action);

    typedef struct BH_InterfaceConfig
    {
        void *user;
        BH_UIActionFn on_action;
        bool enable_hot_reload;
        uint32_t hot_reload_interval_ms;
    } BH_InterfaceConfig;

    /* Lifetime */
    BH_Interface *BH_Interface_Create(BH_UI *ui, const BH_InterfaceConfig *cfg);
    void BH_Interface_Destroy(BH_Interface *iface);

    /* Per-frame */
    void BH_Interface_Update(BH_Interface *iface, double frame_dt_s);

    /* Input routing.
       @return true if the interface consumed the event and it should not be treated as game input.
    */
    bool BH_Interface_HandleEvent(BH_Interface *iface, const SDL_Event *e);

    /* View management */
    bool BH_Interface_Show(BH_Interface *iface, const char *view_type);
    bool BH_Interface_Hide(BH_Interface *iface, const char *view_type);
    bool BH_Interface_Toggle(BH_Interface *iface, const char *view_type);
    bool BH_Interface_IsVisible(const BH_Interface *iface, const char *view_type);

    /* Capture / focus queries */
    bool BH_Interface_IsBlocking(const BH_Interface *iface);
    bool BH_Interface_WantsMouse(const BH_Interface *iface);
    bool BH_Interface_WantsKeyboard(const BH_Interface *iface);

    /* True if the UI currently needs SDL text input events (SDL_EVENT_TEXT_INPUT).
       This should only be true when a text entry widget (eg. <input>, <textarea>) is focused.
    */
    bool BH_Interface_WantsTextInput(const BH_Interface *iface);

    bool BH_Interface_HasKeyboardFocus(const BH_Interface *iface);
    bool BH_Interface_HasMouseOver(const BH_Interface *iface);

    /* Host communication (views can call this to signal game/app actions). */
    void BH_Interface_EmitAction(BH_Interface *iface, const char *action);

    /* Convenience accessors */
    BH_UI *BH_Interface_GetUI(BH_Interface *iface);
    SDL_Window *BH_Interface_GetWindow(BH_Interface *iface);
    void *BH_Interface_GetUser(BH_Interface *iface);

#ifdef __cplusplus
}
#endif
