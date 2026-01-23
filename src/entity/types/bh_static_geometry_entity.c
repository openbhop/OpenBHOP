/* -----------------------------------------------------------------------------
   bh_static_geometry_entity.c
   ----------------------------------------------------------------------------- */
#include "bh_static_geometry_entity.h"
#include "../../render/bh_mesh.h"

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static void bh_static_geometry_destroy(BH_Entity *e, const BH_EntityServices *sv)
{
    BH_StaticGeometryEntity *w = (BH_StaticGeometryEntity *)e;

    for (uint32_t i = 0; i < w->mesh_count; ++i)
    {
        BH_Mesh_Release(&w->meshes[i], sv->gpu_device);
    }
}

static const BH_EntityVTable g_vt = {
    .type_name = "bh_static_geometry",
    .destroy = bh_static_geometry_destroy,
};

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

const BH_EntityVTable *bh_static_geometry_entity_vtable(void)
{
    return &g_vt;
}

BH_StaticGeometryEntity *bh_static_geometry_entity_create(BH_Arena *arena)
{
    return (BH_StaticGeometryEntity *)BH_Entity_Alloc(arena, sizeof(BH_StaticGeometryEntity), &g_vt);
}