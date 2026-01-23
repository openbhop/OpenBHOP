/* -----------------------------------------------------------------------------
   bh_mesh.c
   ----------------------------------------------------------------------------- */
#include "bh_mesh.h"
#include "bh_material.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static bool bh_gpu_upload_buffer(SDL_GPUDevice *device, SDL_GPUBuffer *dst, const void *src, uint32_t size)
{
    assert(device && dst && src);
    assert(size > 0);

    SDL_GPUTransferBufferCreateInfo tci = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size};

    SDL_GPUTransferBuffer *tbuf = SDL_CreateGPUTransferBuffer(device, &tci);
    if (!tbuf)
    {
        SDL_Log("[bh] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, tbuf, false);
    if (!mapped)
    {
        SDL_Log("[bh] SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        return false;
    }

    memcpy(mapped, src, size);
    SDL_UnmapGPUTransferBuffer(device, tbuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
    {
        SDL_Log("[bh] SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        return false;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src_loc = {.transfer_buffer = tbuf, .offset = 0};
    SDL_GPUBufferRegion dst_reg = {.buffer = dst, .offset = 0, .size = size};

    SDL_UploadToGPUBuffer(copy, &src_loc, &dst_reg, false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd))
    {
        SDL_Log("[bh] SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(device, tbuf);
    return true;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Mesh_CreateCube(BH_Mesh *out_mesh, SDL_GPUDevice *device, BH_Arena *permanent_arena,
                        float half_extent_hammer_units, const BH_Material *mat_a, const BH_Material *mat_b)
{
    if (!out_mesh || !device || !permanent_arena)
    {
        return false;
    }

    *out_mesh = (BH_Mesh){0};

    const float h = half_extent_hammer_units;

    /* 24 unique vertices (4 per face) */
    const BH_Vertex v[24] = {
        /* +X */
        {{+h, +h, +h}, {+1, 0, 0}, {0, 0}, {0, 0}},
        {{+h, -h, +h}, {+1, 0, 0}, {1, 0}, {0, 0}},
        {{+h, -h, -h}, {+1, 0, 0}, {1, 1}, {0, 0}},
        {{+h, +h, -h}, {+1, 0, 0}, {0, 1}, {0, 0}},
        /* -X */
        {{-h, -h, +h}, {-1, 0, 0}, {0, 0}, {0, 0}},
        {{-h, +h, +h}, {-1, 0, 0}, {1, 0}, {0, 0}},
        {{-h, +h, -h}, {-1, 0, 0}, {1, 1}, {0, 0}},
        {{-h, -h, -h}, {-1, 0, 0}, {0, 1}, {0, 0}},
        /* +Y */
        {{-h, +h, +h}, {0, +1, 0}, {0, 0}, {0, 0}},
        {{+h, +h, +h}, {0, +1, 0}, {1, 0}, {0, 0}},
        {{+h, +h, -h}, {0, +1, 0}, {1, 1}, {0, 0}},
        {{-h, +h, -h}, {0, +1, 0}, {0, 1}, {0, 0}},
        /* -Y */
        {{+h, -h, +h}, {0, -1, 0}, {0, 0}, {0, 0}},
        {{-h, -h, +h}, {0, -1, 0}, {1, 0}, {0, 0}},
        {{-h, -h, -h}, {0, -1, 0}, {1, 1}, {0, 0}},
        {{+h, -h, -h}, {0, -1, 0}, {0, 1}, {0, 0}},
        /* +Z */
        {{-h, -h, +h}, {0, 0, +1}, {0, 0}, {0, 0}},
        {{+h, -h, +h}, {0, 0, +1}, {1, 0}, {0, 0}},
        {{+h, +h, +h}, {0, 0, +1}, {1, 1}, {0, 0}},
        {{-h, +h, +h}, {0, 0, +1}, {0, 1}, {0, 0}},
        /* -Z */
        {{-h, +h, -h}, {0, 0, -1}, {0, 0}, {0, 0}},
        {{+h, +h, -h}, {0, 0, -1}, {1, 0}, {0, 0}},
        {{+h, -h, -h}, {0, 0, -1}, {1, 1}, {0, 0}},
        {{-h, -h, -h}, {0, 0, -1}, {0, 1}, {0, 0}},
    };

    const uint16_t idx[36] = {
        0,  1,  2,  2,  3,  0,  /* +X */
        4,  5,  6,  6,  7,  4,  /* -X */
        8,  9,  10, 10, 11, 8,  /* +Y */
        12, 13, 14, 14, 15, 12, /* -Y */
        16, 17, 18, 18, 19, 16, /* +Z */
        20, 21, 22, 22, 23, 20, /* -Z */
    };

    SDL_GPUBufferCreateInfo vbci = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = sizeof(v)};
    SDL_GPUBufferCreateInfo ibci = {.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = sizeof(idx)};

    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(device, &vbci);
    SDL_GPUBuffer *ib = SDL_CreateGPUBuffer(device, &ibci);

    if (!vb || !ib)
    {
        SDL_Log("[bh] Failed to create GPU buffers: %s", SDL_GetError());
        if (vb)
            SDL_ReleaseGPUBuffer(device, vb);
        if (ib)
            SDL_ReleaseGPUBuffer(device, ib);
        return false;
    }

    if (!bh_gpu_upload_buffer(device, vb, v, sizeof(v)) || !bh_gpu_upload_buffer(device, ib, idx, sizeof(idx)))
    {
        SDL_ReleaseGPUBuffer(device, vb);
        SDL_ReleaseGPUBuffer(device, ib);
        return false;
    }

    BH_Submesh *subs = (BH_Submesh *)BH_Arena_Alloc(permanent_arena, 2 * sizeof(BH_Submesh), 8);
    if (!subs)
    {
        SDL_ReleaseGPUBuffer(device, vb);
        SDL_ReleaseGPUBuffer(device, ib);
        return false;
    }

    subs[0] = (BH_Submesh){.first_index = 0, .index_count = 18, .material = mat_a};
    subs[1] = (BH_Submesh){.first_index = 18, .index_count = 18, .material = mat_b};

    *out_mesh = (BH_Mesh){
        .vertex_buffer = vb,
        .index_buffer = ib,
        .vertex_count = 24,
        .index_count = 36,
        .index_element_size = SDL_GPU_INDEXELEMENTSIZE_16BIT,
        .submeshes = subs,
        .submesh_count = 2,
    };

    return true;
}

bool BH_Mesh_CreateTriangleList(BH_Mesh *out_mesh, SDL_GPUDevice *device, BH_Arena *arena, const BH_Vertex *vertices,
                                uint32_t vertex_count, const BH_Material *material)
{
    if (!out_mesh || !device || !arena || !vertices || vertex_count == 0)
    {
        return false;
    }

    *out_mesh = (BH_Mesh){0};

    const uint32_t index_count = vertex_count;
    const bool use_u32 = (vertex_count > 0xFFFFu);
    const uint32_t idx_stride = use_u32 ? 4u : 2u;
    const uint32_t idx_bytes = index_count * idx_stride;

    void *idx = SDL_malloc(idx_bytes);
    if (!idx)
    {
        return false;
    }

    if (use_u32)
    {
        uint32_t *p = (uint32_t *)idx;
        for (uint32_t i = 0; i < index_count; ++i)
            p[i] = i;
    }
    else
    {
        uint16_t *p = (uint16_t *)idx;
        for (uint32_t i = 0; i < index_count; ++i)
            p[i] = (uint16_t)i;
    }

    SDL_GPUBufferCreateInfo vbci = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vertex_count * sizeof(BH_Vertex)};
    SDL_GPUBufferCreateInfo ibci = {.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = idx_bytes};

    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(device, &vbci);
    SDL_GPUBuffer *ib = SDL_CreateGPUBuffer(device, &ibci);

    if (!vb || !ib)
    {
        SDL_Log("[bh] Failed to create GPU buffers: %s", SDL_GetError());
        if (vb)
            SDL_ReleaseGPUBuffer(device, vb);
        if (ib)
            SDL_ReleaseGPUBuffer(device, ib);
        SDL_free(idx);
        return false;
    }

    const bool ok_upload = bh_gpu_upload_buffer(device, vb, vertices, vertex_count * sizeof(BH_Vertex)) &&
                           bh_gpu_upload_buffer(device, ib, idx, idx_bytes);

    SDL_free(idx);

    if (!ok_upload)
    {
        SDL_ReleaseGPUBuffer(device, vb);
        SDL_ReleaseGPUBuffer(device, ib);
        return false;
    }

    BH_Submesh *subs = (BH_Submesh *)BH_Arena_Alloc(arena, sizeof(BH_Submesh), 8);
    if (!subs)
    {
        SDL_ReleaseGPUBuffer(device, vb);
        SDL_ReleaseGPUBuffer(device, ib);
        return false;
    }

    subs[0] = (BH_Submesh){
        .first_index = 0,
        .index_count = index_count,
        .material = material,
    };

    *out_mesh = (BH_Mesh){
        .vertex_buffer = vb,
        .index_buffer = ib,
        .vertex_count = vertex_count,
        .index_count = index_count,
        .index_element_size = use_u32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT,
        .submeshes = subs,
        .submesh_count = 1,
    };

    return true;
}

void BH_Mesh_Release(BH_Mesh *mesh, SDL_GPUDevice *device)
{
    if (!mesh || !device)
    {
        return;
    }

    if (mesh->vertex_buffer)
        SDL_ReleaseGPUBuffer(device, mesh->vertex_buffer);
    if (mesh->index_buffer)
        SDL_ReleaseGPUBuffer(device, mesh->index_buffer);

    *mesh = (BH_Mesh){0};
}