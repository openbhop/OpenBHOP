#include "bh_test_cube_entity.h"

#include "../../scene/bh_scene.h"

#include "../../render/bh_material_manager.h"

#include <SDL3/SDL.h>

static void bh_test_cube_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    if (!e || !e->node || !sv || !sv->gpu_device || !sv->permanent_arena)
    {
        return;
    }

    BH_TestCubeEntity *tc = (BH_TestCubeEntity *)e;

    const float default_half_extent = 8.0f;
    const float half_extent = BH_Entity_KVGetFloat32(e, "cube_size", default_half_extent);

    const BH_Material *mat_a = NULL;
    const BH_Material *mat_b = NULL;

    const char *mat_path = BH_Entity_KVGetString(e, "material", NULL);
    if (mat_path && mat_path[0] && sv->materials)
    {
        const BH_Material *m = BH_MaterialManager_Load(sv->materials, mat_path);
        if (m)
        {
            mat_a = m;
            mat_b = m;
        }
    }

    if (!BH_Mesh_CreateCube(&tc->mesh, (SDL_GPUDevice *)sv->gpu_device, sv->permanent_arena, half_extent, mat_a, mat_b))
    {
        SDL_Log("[bh] bh_test_cube: mesh creation failed");
        return;
    }

    tc->mesh_created = true;

    /* Mark node as renderable. */
    e->node->mesh = &tc->mesh;
    e->node->material_override = NULL;
}

static void bh_test_cube_fixed_update(BH_Entity *e, const BH_EntityServices *sv, const BH_Intent *intent, float dt)
{
    (void)sv;
    (void)intent;

    if (!e || !e->node)
    {
        return;
    }

    const float rot_speed = BH_Entity_KVGetFloat32(e, "cube_rotation_speed", 0.8f); /* rad/s */

    const quat dq = quat_fromaxisangle((vec3){0, 0, 1}, rot_speed * dt);
    e->node->local_curr.rotation = quat_norm(quat_mul(e->node->local_curr.rotation, dq));
}

static void bh_test_cube_destroy(BH_Entity *e, const BH_EntityServices *sv)
{
    if (!e || !sv || !sv->gpu_device)
    {
        return;
    }

    BH_TestCubeEntity *tc = (BH_TestCubeEntity *)e;
    if (tc->mesh_created)
    {
        BH_Mesh_Release(&tc->mesh, (SDL_GPUDevice *)sv->gpu_device);
        tc->mesh_created = false;
    }
}

static const BH_EntityVTable g_vt = {
    .type_name = "bh_test_cube",
    .awake = bh_test_cube_awake,
    .fixed_update = bh_test_cube_fixed_update,
    .destroy = bh_test_cube_destroy,
};

const BH_EntityVTable *bh_test_cube_entity_vtable(void)
{
    return &g_vt;
}

BH_TestCubeEntity *bh_test_cube_entity_create(BH_Arena *arena)
{
    return (BH_TestCubeEntity *)BH_Entity_Alloc(arena, sizeof(BH_TestCubeEntity), bh_test_cube_entity_vtable());
}
