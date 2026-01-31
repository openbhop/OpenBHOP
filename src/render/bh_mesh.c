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

static bool bh_gpu_upload_buffer(BH_GPUDevice *device, BH_GPUBuffer *dst, const void *src, uint32_t size)
{
    assert(device && dst && src);
    assert(size > 0);

    BH_GPUTransferBufferCreateInfo tci = {.usage = BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size};

    BH_GPUTransferBuffer *tbuf = BH_GPU_CreateTransferBuffer(device, &tci);
    if (!tbuf)
    {
        SDL_Log("[bh] BH_GPU_CreateTransferBuffer failed: %s", BH_GPU_GetLastError());
        return false;
    }

    void *mapped = BH_GPU_MapTransferBuffer(device, tbuf, false);
    if (!mapped)
    {
        SDL_Log("[bh] BH_GPU_MapTransferBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        return false;
    }

    memcpy(mapped, src, size);
    BH_GPU_UnmapTransferBuffer(device, tbuf);

    BH_GPUCommandBuffer *cmd = BH_GPU_AcquireCommandBuffer(device);
    if (!cmd)
    {
        SDL_Log("[bh] BH_GPU_AcquireCommandBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        return false;
    }

    BH_GPUCopyPass *copy = BH_GPU_BeginCopyPass(cmd);

    BH_GPUTransferBufferLocation src_loc = {.transfer_buffer = tbuf, .offset = 0};
    BH_GPUBufferRegion dst_reg = {.buffer = dst, .offset = 0, .size = size};

    BH_GPU_UploadToBuffer(copy, &src_loc, &dst_reg, false);
    BH_GPU_EndCopyPass(copy);

    if (!BH_GPU_SubmitCommandBuffer(cmd))
    {
        SDL_Log("[bh] bh_mesh.c BH_GPU_SubmitCommandBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        return false;
    }

    BH_GPU_ReleaseTransferBuffer(device, tbuf);
    return true;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Mesh_CreateCube(BH_Mesh *out_mesh, BH_GPUDevice *device, BH_Arena *permanent_arena,
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

    BH_GPUBufferCreateInfo vbci = {.usage = BH_GPU_BUFFERUSAGE_VERTEX, .size = (uint32_t)sizeof(v)};
    BH_GPUBufferCreateInfo ibci = {.usage = BH_GPU_BUFFERUSAGE_INDEX, .size = (uint32_t)sizeof(idx)};

    BH_GPUBuffer *vb = BH_GPU_CreateBuffer(device, &vbci);
    BH_GPUBuffer *ib = BH_GPU_CreateBuffer(device, &ibci);

    if (!vb || !ib)
    {
        SDL_Log("[bh] Failed to create GPU buffers: %s", BH_GPU_GetLastError());
        if (vb)
            BH_GPU_ReleaseBuffer(device, vb);
        if (ib)
            BH_GPU_ReleaseBuffer(device, ib);
        return false;
    }

    if (!bh_gpu_upload_buffer(device, vb, v, sizeof(v)) || !bh_gpu_upload_buffer(device, ib, idx, sizeof(idx)))
    {
        BH_GPU_ReleaseBuffer(device, vb);
        BH_GPU_ReleaseBuffer(device, ib);
        return false;
    }

    BH_Submesh *subs = (BH_Submesh *)BH_Arena_Alloc(permanent_arena, 2 * sizeof(BH_Submesh), 8);
    if (!subs)
    {
        BH_GPU_ReleaseBuffer(device, vb);
        BH_GPU_ReleaseBuffer(device, ib);
        return false;
    }

    subs[0] = (BH_Submesh){.first_index = 0, .index_count = 18, .material = mat_a};
    subs[1] = (BH_Submesh){.first_index = 18, .index_count = 18, .material = mat_b};

    *out_mesh = (BH_Mesh){
        .vertex_buffer = vb,
        .index_buffer = ib,
        .vertex_count = 24,
        .index_count = 36,
        .index_element_size = BH_GPU_INDEXELEMENTSIZE_16BIT,
        .submeshes = subs,
        .submesh_count = 2,
    };

    return true;
}

bool BH_Mesh_CreateTriangleList(BH_Mesh *out_mesh, BH_GPUDevice *device, BH_Arena *arena, const BH_Vertex *vertices,
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

    BH_GPUBufferCreateInfo vbci = {.usage = BH_GPU_BUFFERUSAGE_VERTEX, .size = vertex_count * (uint32_t)sizeof(BH_Vertex)};
    BH_GPUBufferCreateInfo ibci = {.usage = BH_GPU_BUFFERUSAGE_INDEX, .size = idx_bytes};

    BH_GPUBuffer *vb = BH_GPU_CreateBuffer(device, &vbci);
    BH_GPUBuffer *ib = BH_GPU_CreateBuffer(device, &ibci);

    if (!vb || !ib)
    {
        SDL_Log("[bh] Failed to create GPU buffers: %s", BH_GPU_GetLastError());
        if (vb)
            BH_GPU_ReleaseBuffer(device, vb);
        if (ib)
            BH_GPU_ReleaseBuffer(device, ib);
        SDL_free(idx);
        return false;
    }

    const bool ok_upload = bh_gpu_upload_buffer(device, vb, vertices, vertex_count * sizeof(BH_Vertex)) &&
                           bh_gpu_upload_buffer(device, ib, idx, idx_bytes);

    SDL_free(idx);

    if (!ok_upload)
    {
        BH_GPU_ReleaseBuffer(device, vb);
        BH_GPU_ReleaseBuffer(device, ib);
        return false;
    }

    BH_Submesh *subs = (BH_Submesh *)BH_Arena_Alloc(arena, sizeof(BH_Submesh), 8);
    if (!subs)
    {
        BH_GPU_ReleaseBuffer(device, vb);
        BH_GPU_ReleaseBuffer(device, ib);
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
        .index_element_size = use_u32 ? BH_GPU_INDEXELEMENTSIZE_32BIT : BH_GPU_INDEXELEMENTSIZE_16BIT,
        .submeshes = subs,
        .submesh_count = 1,
    };

    return true;
}

void BH_Mesh_Release(BH_Mesh *mesh, BH_GPUDevice *device)
{
    if (!mesh || !device)
    {
        return;
    }

    if (mesh->vertex_buffer)
        BH_GPU_ReleaseBuffer(device, mesh->vertex_buffer);
    if (mesh->index_buffer)
        BH_GPU_ReleaseBuffer(device, mesh->index_buffer);

    *mesh = (BH_Mesh){0};
}