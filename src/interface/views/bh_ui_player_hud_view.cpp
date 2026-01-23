#include "bh_ui_player_hud_view.h"

#include "../bh_interface.h"
#include "../bh_ui_registry.h"

#include "entity/types/bh_player_entity.h"
#include "bh_game.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <string_view>

namespace
{

class BH_UIPlayerHudView final : public BH_UIView
{
  public:
    static constexpr std::string_view kTypeName = "player_hud";
    static constexpr std::string_view kRmlPath = "interface/player_hud.rml";
    static constexpr const char *kModelName = "player";

    std::string_view TypeName() const override
    {
        return kTypeName;
    }
    std::string_view RmlDocumentPath() const override
    {
        return kRmlPath;
    }

  protected:
    bool OnPreLoad(BH_Interface *iface, Rml::Context *ctx) override
    {
        game_ = reinterpret_cast<BH_Game *>(BH_Interface_GetUser(iface));

        if (!ctx)
        {
            SDL_Log("[bh][ui] player_hud: missing Rml context");
            return false;
        }

        // (Re)create the data model before loading the document so bindings resolve cleanly.
        // We keep the model owned by this view; it is removed during OnUnload().
        ctx->RemoveDataModel(kModelName);

        Rml::DataModelConstructor constructor = ctx->CreateDataModel(kModelName);
        if (!constructor)
        {
            SDL_Log("[bh][ui] player_hud: failed to create data model '%s'", kModelName);
            return false;
        }

        constructor.Bind("speed", &speed_hu_per_s_);
        model_handle_ = constructor.GetModelHandle();

        return true;
    }

    bool OnLoad(BH_Interface * /*iface*/) override
    {
        // No explicit element wiring required; this view is fully data-driven.
        return true;
    }

    void OnUnload(BH_Interface * /*iface*/) override
    {
        // Tear down the model to avoid dangling bound pointers.
        if (Rml::ElementDocument *doc = Document())
        {
            if (Rml::Context *ctx = doc->GetContext())
            {
                ctx->RemoveDataModel(kModelName);
            }
        }
        model_handle_ = Rml::DataModelHandle{};
    }

    void OnUpdate(BH_Interface * /*iface*/, float /*dt*/) override
    {
        float new_speed = 0.0f;

        if (game_ && game_->player_node && game_->player_node->entity)
        {
            BH_Entity *e = game_->player_node->entity;
            if (e->vt && e->vt->type_name && SDL_strcmp(e->vt->type_name, "bh_player") == 0)
            {
                BH_PlayerEntity *player = reinterpret_cast<BH_PlayerEntity *>(e);

                float vx = player->state.velocity.x;
                float vy = player->state.velocity.y;

                new_speed = sqrtf(vx * vx + vy * vy);
            }
        }

        // Avoid dirtying the model if nothing changed meaningfully.
        if (std::fabs(new_speed - speed_hu_per_s_) > 0.01f)
        {
            speed_hu_per_s_ = std::round(new_speed);
            if (model_handle_)
            {
                model_handle_.DirtyVariable("speed");
            }
        }
    }

  private:
    BH_Game *game_ = nullptr;

    float speed_hu_per_s_ = 0.0f;
    Rml::DataModelHandle model_handle_;
};

BH_REGISTER_VIEW(BH_UIPlayerHudView, "player_hud");

} // namespace
