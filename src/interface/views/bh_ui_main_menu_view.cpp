#include "bh_ui_main_menu_view.h"

#include "../bh_interface.h"
#include "../bh_rml_event_binding.h"
#include "../bh_ui_registry.h"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <SDL3/SDL.h>

#include <string_view>

namespace
{

class BH_UIMainMenuView final : public BH_UIView
{
  public:
    static constexpr std::string_view kTypeName = "main_menu";
    static constexpr std::string_view kRmlPath = "interface/menu.rml";

    std::string_view TypeName() const override
    {
        return kTypeName;
    }
    std::string_view RmlDocumentPath() const override
    {
        return kRmlPath;
    }

    BH_UIViewFlags Flags() const override
    {
        return BH_UIViewFlags::BlockGameInput | BH_UIViewFlags::CaptureMouse | BH_UIViewFlags::CaptureKeyboard;
    }

  protected:
    bool OnLoad(BH_Interface *iface) override
    {
        Rml::ElementDocument *doc = Document();
        if (!doc)
        {
            SDL_Log("[bh][ui] main_menu: missing document");
            return false;
        }

        play_.Bind(doc, "play_button", "click", [iface](Rml::Event &) {
            if (!iface)
                return;

            (void)BH_Interface_Hide(iface, "main_menu");
            BH_Interface_EmitAction(iface, "play");
        });

        quit_.Bind(doc, "quit_button", "click", [iface](Rml::Event &) {
            if (!iface)
                return;

            BH_Interface_EmitAction(iface, "quit");
        });

        return true;
    }

    void OnUnload(BH_Interface * /*iface*/) override
    {
        play_.Unbind();
        quit_.Unbind();
    }

  private:
    BH_Rml::EventBinding play_;
    BH_Rml::EventBinding quit_;
};

BH_REGISTER_VIEW(BH_UIMainMenuView, "main_menu");

} // namespace
