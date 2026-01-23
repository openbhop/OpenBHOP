#include "bh_interface.h"

#include "bh_ui.h"
#include "bh_ui_view.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bh_ui_hot_reload.h"

struct BH_Interface
{
    BH_UI *ui = nullptr;
    SDL_Window *window = nullptr;
    Rml::Context *context = nullptr;

    std::string asset_root;
    BH_UiHotReloader hot_reloader;

    BH_InterfaceConfig cfg{};

    std::vector<std::unique_ptr<BH_UIView>> views;
};

namespace
{
static bool bh_is_valid_view_type(std::string_view type)
{
    return (!type.empty());
}

static BH_UIView *bh_interface_find_view(BH_Interface *iface, std::string_view type_name)
{
    if (!iface || !bh_is_valid_view_type(type_name))
        return nullptr;

    for (const auto &v : iface->views)
    {
        if (v && v->TypeName() == type_name)
        {
            return v.get();
        }
    }
    return nullptr;
}

static const BH_UIView *bh_interface_find_view(const BH_Interface *iface, std::string_view type_name)
{
    if (!iface || !bh_is_valid_view_type(type_name))
        return nullptr;

    for (const auto &v : iface->views)
    {
        if (v && v->TypeName() == type_name)
        {
            return v.get();
        }
    }
    return nullptr;
}

static BH_UIView *bh_interface_ensure_view(BH_Interface *iface, std::string_view type_name)
{
    if (!iface || !bh_is_valid_view_type(type_name))
        return nullptr;

    if (BH_UIView *existing = bh_interface_find_view(iface, type_name))
    {
        return existing;
    }

    std::unique_ptr<BH_UIView> created = bh_ui_view_create_by_type(type_name);
    if (!created)
    {
        SDL_Log("[bh][iface] Failed to create view: %.*s", int(type_name.size()), type_name.data());
        return nullptr;
    }

    iface->views.push_back(std::move(created));
    return iface->views.back().get();
}

static BH_UIViewFlags bh_interface_visible_flags(const BH_Interface *iface)
{
    BH_UIViewFlags flags = BH_UIViewFlags::None;

    if (!iface)
        return flags;

    for (const auto &v : iface->views)
    {
        if (v && v->IsVisible())
        {
            flags |= v->Flags();
        }
    }

    return flags;
}

static Rml::Input::KeyModifier bh_rml_key_mods()
{
    int mods = 0;
    const SDL_Keymod m = SDL_GetModState();

    if (m & SDL_KMOD_SHIFT)
        mods |= Rml::Input::KM_SHIFT;
    if (m & SDL_KMOD_CTRL)
        mods |= Rml::Input::KM_CTRL;
    if (m & SDL_KMOD_ALT)
        mods |= Rml::Input::KM_ALT;
    if (m & SDL_KMOD_GUI)
        mods |= Rml::Input::KM_META;
    if (m & SDL_KMOD_CAPS)
        mods |= Rml::Input::KM_CAPSLOCK;
    if (m & SDL_KMOD_NUM)
        mods |= Rml::Input::KM_NUMLOCK;
    if (m & SDL_KMOD_SCROLL)
        mods |= Rml::Input::KM_SCROLLLOCK;

    return static_cast<Rml::Input::KeyModifier>(mods);
}

static Rml::Input::KeyIdentifier bh_rml_key_from_sdl(SDL_Keycode key)
{
    using namespace Rml::Input;

    switch (key)
    {
    case SDLK_UNKNOWN:
        return KI_UNKNOWN;
    case SDLK_RETURN:
        return KI_RETURN;
    case SDLK_ESCAPE:
        return KI_ESCAPE;
    case SDLK_BACKSPACE:
        return KI_BACK;
    case SDLK_TAB:
        return KI_TAB;
    case SDLK_SPACE:
        return KI_SPACE;
    case SDLK_LEFT:
        return KI_LEFT;
    case SDLK_RIGHT:
        return KI_RIGHT;
    case SDLK_UP:
        return KI_UP;
    case SDLK_DOWN:
        return KI_DOWN;
    case SDLK_INSERT:
        return KI_INSERT;
    case SDLK_DELETE:
        return KI_DELETE;
    case SDLK_HOME:
        return KI_HOME;
    case SDLK_END:
        return KI_END;
    case SDLK_PAGEUP:
        return KI_PRIOR;
    case SDLK_PAGEDOWN:
        return KI_NEXT;
    default:
        break;
    }

    if (key >= SDLK_A && key <= SDLK_Z)
    {
        return Rml::Input::KeyIdentifier(int(KI_A) + int(key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9)
    {
        return Rml::Input::KeyIdentifier(int(KI_0) + int(key - SDLK_0));
    }

    return KI_UNKNOWN;
}

static bool bh_rml_has_keyboard_focus(Rml::Context *ctx)
{
    if (!ctx)
        return false;

    Rml::Element *focus = ctx->GetFocusElement();
    if (!focus || focus == ctx->GetRootElement())
        return false;

    return true;
}

static bool bh_rml_has_mouse_over(Rml::Context *ctx)
{
    if (!ctx)
        return false;

    Rml::Element *hover = ctx->GetHoverElement();
    if (!hover || hover == ctx->GetRootElement())
        return false;

    return true;
}

} // namespace

extern "C"
{

    BH_Interface *BH_Interface_Create(BH_UI *ui, const BH_InterfaceConfig *cfg)
    {
        if (!ui)
        {
            SDL_Log("[bh][iface] bh_interface_create: ui is null");
            return nullptr;
        }

        BH_Interface *iface = new (std::nothrow) BH_Interface();
        if (!iface)
        {
            SDL_Log("[bh][iface] Out of memory (BH_Interface)");
            return nullptr;
        }

        iface->ui = ui;
        iface->window = BH_UI_GetWindow(ui);
        iface->context = BH_UI_GetRmlContext(ui);
        iface->asset_root = BH_UI_GetAssetRoot(ui);
        iface->hot_reloader.Init(iface->asset_root);

        if (cfg)
        {
            iface->cfg = *cfg;
        }

        if (!iface->window || !iface->context)
        {
            SDL_Log("[bh][iface] Invalid ui: missing window/context");
            delete iface;
            return nullptr;
        }

        return iface;
    }

    void BH_Interface_Destroy(BH_Interface *iface)
    {
        if (!iface)
            return;

        // Unload all views first (detaches listeners before closing documents).
        for (auto &v : iface->views)
        {
            if (v)
            {
                v->Unload(iface);
            }
        }
        iface->views.clear();

        // Ensure deferred document destructions run.
        if (iface->context)
        {
            iface->context->Update();
        }

        delete iface;
    }

    void BH_Interface_Update(BH_Interface *iface, double frame_dt_s)
    {
        if (!iface || !iface->context || !iface->window)
            return;

        frame_dt_s = std::clamp(frame_dt_s, 0.0, 0.25);

        // Resize context.
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(iface->window, &w, &h);
        iface->context->SetDimensions(Rml::Vector2i(w, h));

        const float display_scale = SDL_GetWindowDisplayScale(iface->window);
        if (display_scale > 0.f)
        {
            iface->context->SetDensityIndependentPixelRatio(display_scale);
        }

        // Hot reload RML/RCSS during development.
        // We poll the assets/interface directory and reload any loaded documents/views when changes are detected.
        {
            const std::vector<BH_UiHotReloadChange> changes = iface->hot_reloader.Poll();
            if (!changes.empty())
            {
                bool changed_rml = false;
                bool changed_rcss = false;

                for (const BH_UiHotReloadChange &c : changes)
                {
                    if (c.kind == BH_UiHotReloadKind::Rcss)
                        changed_rcss = true;
                    else
                        changed_rml = true;
                }

                if (changed_rcss)
                {
                    Rml::Factory::ClearStyleSheetCache();
                }

                if (changed_rml)
                {
                    Rml::Factory::ClearTemplateCache();

                    for (auto &v : iface->views)
                    {
                        if (v && v->IsLoaded())
                        {
                            (void)v->Reload(iface, iface->context);
                        }
                    }
                }
                else if (changed_rcss)
                {
                    for (auto &v : iface->views)
                    {
                        if (v && v->IsLoaded() && v->Document())
                        {
                            v->Document()->ReloadStyleSheet();
                        }
                    }
                }
            }
        }

        // Per-view updates.
        for (auto &v : iface->views)
        {
            if (v && v->IsVisible())
            {
                v->Update(iface, static_cast<float>(frame_dt_s));
            }
        }

        iface->context->Update();
    }

    bool BH_Interface_HandleEvent(BH_Interface *iface, const SDL_Event *e)
    {
        if (!iface || !iface->context || !iface->window || !e)
            return false;

        // Only route input-related events.
        switch (e->type)
        {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            break;
        default:
            return false;
        }

        const Rml::Input::KeyModifier mods = bh_rml_key_mods();
        const float pixel_density = SDL_GetWindowPixelDensity(iface->window);

        auto to_px = [&](float v) -> int { return int(v * (pixel_density > 0.f ? pixel_density : 1.f)); };

        // RmlUi returns "propagate further?" (existing code treated it that way).
        bool propagate = true;

        switch (e->type)
        {
        case SDL_EVENT_MOUSE_MOTION:
            propagate = iface->context->ProcessMouseMove(to_px(e->motion.x), to_px(e->motion.y), mods);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int button_index = 0;
            if (e->button.button == SDL_BUTTON_LEFT)
                button_index = 0;
            else if (e->button.button == SDL_BUTTON_RIGHT)
                button_index = 1;
            else if (e->button.button == SDL_BUTTON_MIDDLE)
                button_index = 2;
            propagate = iface->context->ProcessMouseButtonDown(button_index, mods);
        }
        break;

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int button_index = 0;
            if (e->button.button == SDL_BUTTON_LEFT)
                button_index = 0;
            else if (e->button.button == SDL_BUTTON_RIGHT)
                button_index = 1;
            else if (e->button.button == SDL_BUTTON_MIDDLE)
                button_index = 2;
            propagate = iface->context->ProcessMouseButtonUp(button_index, mods);
        }
        break;

        case SDL_EVENT_MOUSE_WHEEL: {
            const float wheel_y = float(-e->wheel.y);
            propagate = iface->context->ProcessMouseWheel(wheel_y, mods);
        }
        break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            propagate = iface->context->ProcessMouseLeave();
            break;

        case SDL_EVENT_KEY_DOWN: {
            const SDL_Keycode key = e->key.key;

            // Global hotkeys (developer convenience)
            // Toggle developer console with backquote / grave.
            if (key == SDLK_GRAVE && !e->key.repeat)
            {
                (void)BH_Interface_Toggle(iface, "dev_console");
                return true; // consumed
            }

            const auto rml_key = bh_rml_key_from_sdl(key);
            propagate = iface->context->ProcessKeyDown(rml_key, mods);

            // Let the game handle Escape (close / pause / etc).
            if (key == SDLK_ESCAPE)
                return false;
        }
        break;

        case SDL_EVENT_KEY_UP: {
            const SDL_Keycode key = e->key.key;
            const auto rml_key = bh_rml_key_from_sdl(key);
            propagate = iface->context->ProcessKeyUp(rml_key, mods);

            if (key == SDLK_ESCAPE)
                return false;
        }
        break;

        case SDL_EVENT_TEXT_INPUT:
            propagate = iface->context->ProcessTextInput(Rml::String(e->text.text));
            break;
        }

        if (BH_Interface_IsBlocking(iface))
        {
            // If a modal view is visible, treat input as consumed even if it propagates through Rml.
            return true;
        }

        // If Rml wants to propagate, we did NOT consume the event.
        return !propagate;
    }

    bool BH_Interface_Show(BH_Interface *iface, const char *view_type)
    {
        if (!iface || !view_type || !view_type[0])
            return false;

        BH_UIView *view = bh_interface_ensure_view(iface, view_type);
        if (!view)
            return false;

        if (!view->EnsureLoaded(iface, iface->context))
            return false;

        view->Show(iface);
        return true;
    }

    bool BH_Interface_Hide(BH_Interface *iface, const char *view_type)
    {
        if (!iface || !view_type || !view_type[0])
            return false;

        BH_UIView *view = bh_interface_find_view(iface, view_type);
        if (!view)
            return false;

        view->Hide(iface);
        return true;
    }

    bool BH_Interface_Toggle(BH_Interface *iface, const char *view_type)
    {
        if (!iface || !view_type || !view_type[0])
            return false;

        if (BH_Interface_IsVisible(iface, view_type))
            return BH_Interface_Hide(iface, view_type);

        return BH_Interface_Show(iface, view_type);
    }

    bool BH_Interface_IsVisible(const BH_Interface *iface, const char *view_type)
    {
        if (!iface || !view_type || !view_type[0])
            return false;

        const BH_UIView *view = bh_interface_find_view(iface, view_type);
        return view ? view->IsVisible() : false;
    }

    bool BH_Interface_IsBlocking(const BH_Interface *iface)
    {
        const BH_UIViewFlags flags = bh_interface_visible_flags(iface);
        return BH_UIView_HasFlag(flags, BH_UIViewFlags::BlockGameInput);
    }

    bool BH_Interface_WantsMouse(const BH_Interface *iface)
    {
        if (!iface || !iface->context)
            return false;

        const BH_UIViewFlags flags = bh_interface_visible_flags(iface);
        if (BH_UIView_HasFlag(flags, BH_UIViewFlags::CaptureMouse))
            return true;

        return bh_rml_has_mouse_over(iface->context);
    }

    bool BH_Interface_WantsKeyboard(const BH_Interface *iface)
    {
        if (!iface || !iface->context)
            return false;

        const BH_UIViewFlags flags = bh_interface_visible_flags(iface);
        if (BH_UIView_HasFlag(flags, BH_UIViewFlags::CaptureKeyboard))
            return true;

        return bh_rml_has_keyboard_focus(iface->context);
    }

    bool BH_Interface_WantsTextInput(const BH_Interface *iface)
    {
        if (!iface || !iface->context)
            return false;

        Rml::Element *focus = iface->context->GetFocusElement();
        if (!focus)
            return false;

        // Text input should only be enabled when a text entry widget is focused.
        if (rmlui_dynamic_cast<Rml::ElementFormControlInput *>(focus))
            return true;

        if (rmlui_dynamic_cast<Rml::ElementFormControlTextArea *>(focus))
            return true;

        return false;
    }

    bool BH_Interface_HasKeyboardFocus(const BH_Interface *iface)
    {
        return iface && bh_rml_has_keyboard_focus(iface->context);
    }

    bool BH_Interface_HasMouseOver(const BH_Interface *iface)
    {
        return iface && bh_rml_has_mouse_over(iface->context);
    }

    void BH_Interface_EmitAction(BH_Interface *iface, const char *action)
    {
        if (!iface || !action || !action[0])
            return;

        if (iface->cfg.on_action)
        {
            iface->cfg.on_action(iface->cfg.user, action);
        }
    }

    BH_UI *BH_Interface_GetUI(BH_Interface *iface)
    {
        return iface ? iface->ui : nullptr;
    }

    SDL_Window *BH_Interface_GetWindow(BH_Interface *iface)
    {
        return iface ? iface->window : nullptr;
    }

    void *BH_Interface_GetUser(BH_Interface *iface)
    {
        return iface ? iface->cfg.user : nullptr;
    }

} // extern "C"
