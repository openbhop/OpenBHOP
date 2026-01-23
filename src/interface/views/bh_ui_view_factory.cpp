#include "../bh_ui_registry.h"
#include "../bh_ui_view.h"

#include "bh_ui_main_menu_view.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

// -----------------------------
//  BH_UIView lifecycle
// -----------------------------

bool BH_UIView::EnsureLoaded(BH_Interface *iface, Rml::Context *ctx)
{
    if (loaded_)
        return true;

    if (!ctx)
    {
        const auto t = TypeName();
        SDL_Log("[bh][ui] view %.*s: missing Rml context", int(t.size()), t.data());
        return false;
    }

    if (!OnPreLoad(iface, ctx))
    {
        const auto t = TypeName();
        SDL_Log("[bh][ui] view %.*s: OnPreLoad() failed", int(t.size()), t.data());
        return false;
    }

    // Load document if provided.
    const std::string_view rml = RmlDocumentPath();
    if (!rml.empty())
    {
        // Rml wants a null-terminated string.
        const std::string rml_str(rml);
        doc_ = ctx->LoadDocument(rml_str);
        if (!doc_)
        {
            const auto t = TypeName();
            SDL_Log("[bh][ui] view %.*s: failed to load RML: %s", int(t.size()), t.data(), rml_str.c_str());
            return false;
        }
    }

    const bool ok = OnLoad(iface);
    loaded_ = ok;

    if (!ok)
    {
        CloseDocument();
    }

    return ok;
}

bool BH_UIView::Reload(BH_Interface *iface, Rml::Context *ctx)
{
    if (!ctx)
    {
        const auto t = TypeName();
        SDL_Log("[bh][ui] view %.*s: Reload() missing Rml context", int(t.size()), t.data());
        return false;
    }

    // If not loaded yet, behave like EnsureLoaded.
    if (!loaded_)
        return EnsureLoaded(iface, ctx);

    const bool was_visible = visible_;

    // Hide to ensure focus/input state is reset cleanly.
    if (was_visible)
    {
        Hide(iface);
    }

    // Close old document and detach resources.
    Unload(iface);

    // RmlUi defers document destruction until Context::Update().
    // Flush so we can safely reload and re-bind without stale documents hanging around.
    ctx->Update();

    if (!EnsureLoaded(iface, ctx))
        return false;

    if (was_visible)
    {
        Show(iface);
    }

    return true;
}

void BH_UIView::Unload(BH_Interface *iface)
{
    if (!loaded_)
    {
        visible_ = false;
        return;
    }

    OnUnload(iface);
    CloseDocument();

    loaded_ = false;
    visible_ = false;
}

void BH_UIView::Show(BH_Interface *iface)
{
    if (doc_)
    {
        doc_->Show();
    }

    visible_ = true;
    OnShow(iface);
}

void BH_UIView::Hide(BH_Interface *iface)
{
    OnHide(iface);

    if (doc_)
    {
        doc_->Hide();
    }

    visible_ = false;
}

void BH_UIView::Update(BH_Interface *iface, float dt)
{
    // BH_Interface already calls Update() only for visible views,
    // but keep this tolerant for direct use.
    if (!visible_)
        return;

    OnUpdate(iface, dt);
}

void BH_UIView::CloseDocument()
{
    if (!doc_)
        return;

    doc_->Close();
    doc_ = nullptr;
}

// -----------------------------
//  Factory
// -----------------------------

std::unique_ptr<BH_UIView> bh_ui_view_create_by_type(std::string_view type_name)
{
    if (type_name.empty())
        return nullptr;

    const auto &registry = BH_GetViewRegistry();

    // O(1) or O(log n) lookup
    if (auto it = registry.find(type_name); it != registry.end())
    {
        return it->second(); // Invoke the lambda created by the macro
    }

    SDL_Log("[bh][iface] Unknown view type: %.*s", int(type_name.size()), type_name.data());
    return nullptr;
}
