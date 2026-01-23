/* -----------------------------------------------------------------------------
   bh_entity_factory.c
   ----------------------------------------------------------------------------- */

#include "bh_entity_factory.h"

#include <string.h>

#include "types/bh_brush_measure.h"
#include "types/bh_light_directional_entity.h"
#include "types/bh_player_entity.h"
#include "types/bh_static_geometry_entity.h"
#include "types/bh_test_cube_entity.h"
#include "types/bh_ui_view_entity.h"

/* -----------------------------------------------------------------------------
    Internal Helpers
    ----------------------------------------------------------------------------- */

typedef struct
{
    const char *alias;
    const BH_EntityVTable *(*vtable_fn)(void);
    size_t instance_size;
} BH_EntityTypeEntry;

static const BH_EntityTypeEntry g_entity_types[] = {
    {.vtable_fn = bh_test_cube_entity_vtable, .instance_size = sizeof(BH_TestCubeEntity)},
    {.vtable_fn = bh_player_entity_vtable, .instance_size = sizeof(BH_PlayerEntity)},
    {.vtable_fn = bh_light_directional_entity_vtable, .instance_size = sizeof(BH_LightDirectionalEntity)},
    {.alias = "env_sun",
     .vtable_fn = bh_light_directional_entity_vtable,
     .instance_size = sizeof(BH_LightDirectionalEntity)},
    {.vtable_fn = bh_static_geometry_entity_vtable, .instance_size = sizeof(BH_StaticGeometryEntity)},
    {.vtable_fn = bh_ui_view_entity_vtable, .instance_size = sizeof(BH_UIViewEntity)},
    {.vtable_fn = bh_brush_measure_vtable, .instance_size = sizeof(BH_BrushMeasure)},
};

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

BH_Entity *BH_Entity_CreateByType(const char *type_name, BH_Arena *arena)
{
    if (!type_name || !type_name[0] || !arena)
    {
        return NULL;
    }

    for (uint32_t i = 0; i < BH_ARRAY_COUNT(g_entity_types); ++i)
    {
        const BH_EntityTypeEntry *e = &g_entity_types[i];
        const BH_EntityVTable *vt = e->vtable_fn();
        const char *match_name = e->alias ? e->alias : vt->type_name;

        if (match_name && strcmp(type_name, match_name) == 0)
        {
            return BH_Entity_Alloc(arena, e->instance_size, vt);
        }
    }

    return NULL;
}