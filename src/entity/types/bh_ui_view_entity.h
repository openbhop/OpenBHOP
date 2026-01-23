/* -----------------------------------------------------------------------------
   bh_ui_view_entity.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "entity/bh_entity.h"
#include "render/bh_mesh.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct BH_UIViewEntity
    {
        BH_Entity base;

        /* Lifecycle state */
        bool initialized;

        /* Offscreen render context */
        struct BH_UIContext *ui_ctx;

        /* World-space presentation */
        BH_Mesh mesh;
        bool mesh_created;

        /* Optimization flags */
        bool has_player_info;
        bool has_fps_counter;
    } BH_UIViewEntity;

    const BH_EntityVTable *bh_ui_view_entity_vtable(void);
    BH_UIViewEntity *bh_ui_view_entity_create(BH_Arena *arena);

#ifdef __cplusplus
}
#endif
