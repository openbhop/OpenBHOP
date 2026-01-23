#pragma once

#include "core/bh_core.h"

#ifndef __cplusplus
#error "bh_ui_view.h is C++-only (views use virtual functions + RAII). Compile UI code as C++."
#endif

#include <cstdint>
#include <memory>
#include <string_view>

struct BH_Interface;

namespace Rml
{
class Context;
class ElementDocument;
} // namespace Rml

// -----------------------------
//  BH_UIViewFlags
// -----------------------------

enum class BH_UIViewFlags : uint32_t
{
    None = 0,

    // When visible, this view should block game simulation / camera control.
    BlockGameInput = 1u << 0,

    // When visible, this view wants the mouse cursor and should suppress relative mouse look.
    CaptureMouse = 1u << 1,

    // When visible, this view wants keyboard input (eg. menus, chat).
    CaptureKeyboard = 1u << 2,
};

constexpr BH_UIViewFlags operator|(BH_UIViewFlags a, BH_UIViewFlags b)
{
    return static_cast<BH_UIViewFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr BH_UIViewFlags operator&(BH_UIViewFlags a, BH_UIViewFlags b)
{
    return static_cast<BH_UIViewFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr BH_UIViewFlags &operator|=(BH_UIViewFlags &a, BH_UIViewFlags b)
{
    a = (a | b);
    return a;
}

constexpr bool BH_UIView_HasFlag(BH_UIViewFlags flags, BH_UIViewFlags bit)
{
    return (static_cast<uint32_t>(flags & bit) != 0u);
}

// -----------------------------
//  BH_UIView
// -----------------------------
//
// C++ view base-class.
// - Implement views by deriving and overriding the small virtual surface.
// - BH_Interface owns views and drives load/show/hide/update/unload.
// - The view owns its Rml document pointer (closed during Unload()).
//

class BH_UIView
{
  public:
    virtual ~BH_UIView() = default;

    // Stable identifier used by bh_interface_show/hide/toggle.
    virtual std::string_view TypeName() const = 0;

    // Optional RML document path. If empty, the view is "logic-only".
    virtual std::string_view RmlDocumentPath() const
    {
        return {};
    }

    // Behavior flags when visible.
    virtual BH_UIViewFlags Flags() const
    {
        return BH_UIViewFlags::None;
    }

    bool IsLoaded() const noexcept
    {
        return loaded_;
    }
    bool IsVisible() const noexcept
    {
        return visible_;
    }
    Rml::ElementDocument *Document() const noexcept
    {
        return doc_;
    }

    // ---- Driven by BH_Interface (treat as internal API) ----
    bool EnsureLoaded(BH_Interface *iface, Rml::Context *ctx);
    bool Reload(BH_Interface *iface, Rml::Context *ctx);
    void Unload(BH_Interface *iface);

    void Show(BH_Interface *iface);
    void Hide(BH_Interface *iface);

    void Update(BH_Interface *iface, float dt);

  protected:
    // Called before the document is loaded (and before OnLoad).
    // Useful for setting up per-context resources that the RML may reference,
    // such as data models.
    virtual bool OnPreLoad(BH_Interface * /*iface*/, Rml::Context * /*ctx*/)
    {
        return true;
    }

    // Called after the document is loaded (or immediately if RmlDocumentPath() is empty).
    // Return false to fail loading this view.
    virtual bool OnLoad(BH_Interface * /*iface*/)
    {
        return true;
    }

    // Called during unload (before the Rml document is closed).
    virtual void OnUnload(BH_Interface * /*iface*/)
    {
    }

    // Visibility notifications.
    virtual void OnShow(BH_Interface * /*iface*/)
    {
    }
    virtual void OnHide(BH_Interface * /*iface*/)
    {
    }

    // Optional per-frame update while visible.
    virtual void OnUpdate(BH_Interface * /*iface*/, float /*dt*/)
    {
    }

    // Valid only while loaded and if RmlDocumentPath() was non-empty and successfully loaded.
    Rml::ElementDocument *doc_ = nullptr;

  private:
    void CloseDocument();

    bool loaded_ = false;
    bool visible_ = false;
};

// Factory: create a view instance by its TypeName().
std::unique_ptr<BH_UIView> bh_ui_view_create_by_type(std::string_view type_name);
