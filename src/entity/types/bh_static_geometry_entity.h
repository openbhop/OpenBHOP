/* -----------------------------------------------------------------------------
   bh_static_geometry_entity.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../../render/bh_mesh.h"
#include "../bh_entity.h"

/*
   Entity owning a mesh array; releases GPU buffers on destroy.
   Used by map loader for static world geometry.
*/
typedef struct BH_StaticGeometryEntity
{
    BH_Entity base;
    BH_Mesh *meshes; /* arena-owned */
    uint32_t mesh_count;
} BH_StaticGeometryEntity;

const BH_EntityVTable *bh_static_geometry_entity_vtable(void);
BH_StaticGeometryEntity *bh_static_geometry_entity_create(BH_Arena *arena);
