#pragma once

#include "../bh_entity.h"

#include "../../render/bh_mesh.h"

typedef struct BH_TestCubeEntity
{
    BH_Entity base;
    struct BH_Mesh mesh;
    bool mesh_created;
} BH_TestCubeEntity;

const BH_EntityVTable *bh_test_cube_entity_vtable(void);
BH_TestCubeEntity *bh_test_cube_entity_create(BH_Arena *arena);
