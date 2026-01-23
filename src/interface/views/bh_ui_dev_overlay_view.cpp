#include "bh_ui_dev_overlay_view.h"

#include "../bh_interface.h"
#include "../bh_ui_registry.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{

class BH_UIDevOverlayView final : public BH_UIView
{
  public:
    static constexpr std::string_view kTypeName = "dev_overlay";
    static constexpr std::string_view kRmlPath = "interface/dev_overlay.rml";

    std::string_view TypeName() const override
    {
        return kTypeName;
    }
    std::string_view RmlDocumentPath() const override
    {
        return kRmlPath;
    }

  protected:
    bool OnLoad(BH_Interface * /*iface*/) override
    {
        Rml::ElementDocument *doc = Document();
        if (!doc)
        {
            SDL_Log("[bh][ui] dev_overlay: missing document");
            return false;
        }

        fps_text_el_ = doc->GetElementById("fps_text");
        if (!fps_text_el_)
        {
            SDL_Log("[bh][ui] dev_overlay: missing element #fps_text");
            return false;
        }

        // Initialize with something readable.
        fps_text_el_->SetInnerRML("0 FPS (0.0 ms)");

        return true;
    }

    void OnUnload(BH_Interface * /*iface*/) override
    {
        fps_text_el_ = nullptr;
    }

    void OnUpdate(BH_Interface * /*iface*/, float dt) override
    {
        if (!fps_text_el_)
            return;

        // Exponential moving average for stable readout.
        // (Fast enough to feel responsive, stable enough to not flicker.)
        const float clamped = (dt < 0.0f) ? 0.0f : ((dt > 0.25f) ? 0.25f : dt);
        if (!have_dt_)
        {
            smoothed_dt_ = clamped;
            have_dt_ = true;
        }
        else
        {
            // 0.10 is a good "feel"; tweak if needed.
            smoothed_dt_ = smoothed_dt_ + (clamped - smoothed_dt_) * 0.10f;
        }

        const float frame_ms = smoothed_dt_ * 1000.0f;
        const float fps = (smoothed_dt_ > 0.000001f) ? (1.0f / smoothed_dt_) : 0.0f;

        // Update at a capped rate to avoid churn; ~10Hz is plenty.
        update_accum_ += clamped;
        if (update_accum_ < 0.10f)
            return;
        update_accum_ = 0.0f;

        char buf[64];
        SDL_snprintf(buf, sizeof(buf), "%d FPS (%.1f ms)", (int)std::round(fps), frame_ms);
        fps_text_el_->SetInnerRML(buf);
    }

  private:
    Rml::Element *fps_text_el_ = nullptr;
    bool have_dt_ = false;
    float smoothed_dt_ = 0.0f;
    float update_accum_ = 0.0f;
};

BH_REGISTER_VIEW(BH_UIDevOverlayView, "dev_overlay");

} // namespace
