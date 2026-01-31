/* -----------------------------------------------------------------------------
   bh_mesh.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"
#include "../math/bh_math.h"

#include "bh_gpu.h"

struct BH_Material;

typedef struct BH_Vertex
{
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec2 uv2; /* Lightmap UVs */
} BH_Vertex;

typedef struct BH_Submesh
{
    uint32_t first_index;
    uint32_t index_count;
    const struct BH_Material *material;
} BH_Submesh;

typedef struct BH_Mesh
{
    BH_GPUBuffer *vertex_buffer;
    BH_GPUBuffer *index_buffer;

    uint32_t vertex_count;
    uint32_t index_count;
    BH_GPUIndexElementSize index_element_size;

    BH_Submesh *submeshes;
    uint32_t submesh_count;
} BH_Mesh;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Mesh_CreateCube(BH_Mesh *out_mesh, BH_GPUDevice *device, BH_Arena *permanent_arena,
                        float half_extent_hammer_units, const struct BH_Material *mat_a,
                        const struct BH_Material *mat_b);

/*
Creates mesh from a raw triangle list.
Vertices uploaded as-is. Generates linear index buffer (0..N).
Creates single submesh.
*/
bool BH_Mesh_CreateTriangleList(BH_Mesh *out_mesh, BH_GPUDevice *device, BH_Arena *arena, const BH_Vertex *vertices,
                                uint32_t vertex_count, const struct BH_Material *material);

void BH_Mesh_Release(BH_Mesh *mesh, BH_GPUDevice *device);