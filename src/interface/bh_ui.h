#pragma once

#include "render/bh_renderer.h"

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Opaque UI render/context handle (backed by RmlUi internally).

       This module is intentionally *rendering-focused*:
         - It owns the RmlUi context + render backend integration.
         - It exposes renderer hooks for the engine render pipeline.

       Higher-level UI templates / screens / input routing live in bh_interface.
    */
    typedef struct BH_UI BH_UI;

    /* Opaque offscreen UI context (RmlUi Context + backend + optional render target). */
    typedef struct BH_UIContext BH_UIContext;

    /* Create a UI renderer+context for a given window + renderer.
       You must register the returned render hooks via bh_renderer_add_hooks().
    */
    BH_UI *BH_UI_Create(BH_Renderer *renderer, SDL_Window *window, const char *asset_root);
    void BH_UI_Destroy(BH_UI *ui);

    /* Renderer hook registration.
       The returned struct can be passed to bh_renderer_add_hooks().
    */
    BH_RenderPassHooks BH_UI_GetRenderHooks(const BH_UI *ui);

    /* Convenience accessors (mostly useful to higher-level UI code). */
    SDL_Window *BH_UI_GetWindow(BH_UI *ui);
    const char *BH_UI_GetAssetRoot(const BH_UI *ui);

    /*
      Offscreen contexts

      These allow rendering UI into a user-managed (or UI-managed) texture.
      Typical flow:
        - BH_UIContext_Create()
        - BH_UIContext_SetRenderTarget()
        - BH_UIContext_LoadDocumentFromMemory()
        - BH_UIContext_Render()
        - BH_UIContext_Destroy()
    */
    BH_UIContext *BH_UIContext_Create(BH_UI *ui, const char *name, uint32_t width, uint32_t height);
    void BH_UIContext_Destroy(BH_UI *ui, BH_UIContext *ctx);

    /*
      Creates (or recreates) an owned render target for this context.
      The created color target is also registered with the engine texture manager
      so you can bind it in materials.
    */
    bool BH_UIContext_SetRenderTarget(BH_UIContext *ctx, uint32_t width, uint32_t height);

    /* Returns 0 if no render target was set. */
    BH_TextureHandle BH_UIContext_GetRenderTargetHandle(const BH_UIContext *ctx);
    SDL_GPUTexture *BH_UIContext_GetRenderTargetTexture(const BH_UIContext *ctx);

    /*
      Loads a document from an in-memory RML string and shows it.
      Returns false on parse/load failure.
    */
    bool BH_UIContext_LoadDocumentFromMemory(BH_UIContext *ctx, const char *rml);

    void BH_UIContext_SetElementInnerRML(BH_UIContext *ctx, const char *element_id, const char *rml);

    /* Updates + renders into the current render target (if any). */
    bool BH_UIContext_Render(BH_UIContext *ctx);

#ifdef __cplusplus
    namespace Rml
    {
    class Context;
    }
    /* C++-only: access the underlying RmlUi context. */
    Rml::Context *BH_UI_GetRmlContext(BH_UI *ui);
#endif

#ifdef __cplusplus
}
#endif