/* -----------------------------------------------------------------------------
   bh_light_directional_entity.c
   ----------------------------------------------------------------------------- */

#include "bh_light_directional_entity.h"
#include "../../scene/bh_scene.h"
#include <SDL3/SDL.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static void bh_light_directional_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    BH_Scene *scene = sv->scene;

    /* defaults: pitch down 45 degrees, pure white, full intensity */
    vec3 angles = BH_Entity_KVGetVec3(e, "angles", (vec3){45.0f, 0.0f, 0.0f});
    vec3 color = BH_Entity_KVGetVec3(e, "SunColor", (vec3){1.0f, 1.0f, 1.0f});
    float intensity = BH_Entity_KVGetFloat32(e, "Intensity", 1.0f);

    vec3 dir;
    AngleVectors(angles, &dir, NULL, NULL);

    scene->directional_light.direction = dir;
    scene->directional_light.color = color;
    scene->directional_light.intensity = intensity;
    scene->has_directional_light = true;

    SDL_Log("[bh] directional light set: angles=(%.1f %.1f %.1f) -> dir=(%.2f %.2f %.2f) intensity=%.2f", angles.x,
            angles.y, angles.z, dir.x, dir.y, dir.z, intensity);
}

static const BH_EntityVTable g_vt = {
    .type_name = "light_directional",
    .awake = bh_light_directional_awake,
    .fixed_update = NULL,
    .destroy = NULL,
};

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

const BH_EntityVTable *bh_light_directional_entity_vtable(void)
{
    return &g_vt;
}

BH_LightDirectionalEntity *bh_light_directional_entity_create(BH_Arena *arena)
{
    if (!arena)
    {
        return NULL;
    }

    return (BH_LightDirectionalEntity *)BH_Entity_Alloc(arena, sizeof(BH_LightDirectionalEntity), &g_vt);
}