/* -----------------------------------------------------------------------------
   bh_gpu_sdl.c
   -----------------------------------------------------------------------------

   SDL3 backend for bh_gpu.
*/

#include "bh_gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

/* -----------------------------------------------------------------------------
   Enum / flag mapping helpers
   ----------------------------------------------------------------------------- */

static SDL_GPUShaderFormat bh_to_sdl_shader_format(BH_GPUShaderFormat fmt)
{
    switch (fmt)
    {
    case BH_GPU_SHADERFORMAT_SPIRV:
    default:
        return SDL_GPU_SHADERFORMAT_SPIRV;
    }
}

static SDL_GPUShaderStage bh_to_sdl_shader_stage(BH_GPUShaderStage stage)
{
    switch (stage)
    {
    case BH_GPU_SHADERSTAGE_FRAGMENT:
        return SDL_GPU_SHADERSTAGE_FRAGMENT;
    case BH_GPU_SHADERSTAGE_VERTEX:
    default:
        return SDL_GPU_SHADERSTAGE_VERTEX;
    }
}

static SDL_GPUVertexElementFormat bh_to_sdl_vertex_element_format(BH_GPUVertexElementFormat fmt)
{
    switch (fmt)
    {
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT2:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT4:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case BH_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM:
        return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT3:
    default:
        return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    }
}

static SDL_GPUVertexInputRate bh_to_sdl_vertex_input_rate(BH_GPUVertexInputRate rate)
{
    switch (rate)
    {
    case BH_GPU_VERTEXINPUTRATE_INSTANCE:
        return SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    case BH_GPU_VERTEXINPUTRATE_VERTEX:
    default:
        return SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
}

static SDL_GPUPrimitiveType bh_to_sdl_primitive_type(BH_GPUPrimitiveType type)
{
    switch (type)
    {
    case BH_GPU_PRIMITIVETYPE_LINELIST:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case BH_GPU_PRIMITIVETYPE_TRIANGLELIST:
    default:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
}

static SDL_GPUFillMode bh_to_sdl_fill_mode(BH_GPUFillMode mode)
{
    (void)mode;
    return SDL_GPU_FILLMODE_FILL;
}

static SDL_GPUCullMode bh_to_sdl_cull_mode(BH_GPUCullMode mode)
{
    switch (mode)
    {
    case BH_GPU_CULLMODE_FRONT:
        return SDL_GPU_CULLMODE_FRONT;
    case BH_GPU_CULLMODE_BACK:
        return SDL_GPU_CULLMODE_BACK;
    case BH_GPU_CULLMODE_NONE:
    default:
        return SDL_GPU_CULLMODE_NONE;
    }
}

static SDL_GPUFrontFace bh_to_sdl_front_face(BH_GPUFrontFace ff)
{
    switch (ff)
    {
    case BH_GPU_FRONTFACE_CLOCKWISE:
        return SDL_GPU_FRONTFACE_CLOCKWISE;
    case BH_GPU_FRONTFACE_COUNTER_CLOCKWISE:
    default:
        return SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    }
}

static SDL_GPUBlendFactor bh_to_sdl_blend_factor(BH_GPUBlendFactor f)
{
    switch (f)
    {
    case BH_GPU_BLENDFACTOR_ZERO:
        return SDL_GPU_BLENDFACTOR_ZERO;
    case BH_GPU_BLENDFACTOR_SRC_ALPHA:
        return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BH_GPU_BLENDFACTOR_ONE:
    default:
        return SDL_GPU_BLENDFACTOR_ONE;
    }
}

static SDL_GPUBlendOp bh_to_sdl_blend_op(BH_GPUBlendOp op)
{
    (void)op;
    return SDL_GPU_BLENDOP_ADD;
}

static SDL_GPUSwapchainComposition bh_to_sdl_swapchain_comp(BH_GPUSwapchainComposition comp)
{
    switch (comp)
    {
    case BH_GPU_SWAPCHAINCOMPOSITION_SDR:
    default:
        return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }
}

static SDL_GPUPresentMode bh_to_sdl_present_mode(BH_GPUPresentMode mode)
{
    switch (mode)
    {
    case BH_GPU_PRESENTMODE_VSYNC:
        return SDL_GPU_PRESENTMODE_VSYNC;
    case BH_GPU_PRESENTMODE_IMMEDIATE:
    default:
        return SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
}

static SDL_GPUTextureType bh_to_sdl_texture_type(BH_GPUTextureType type)
{
    switch (type)
    {
    case BH_GPU_TEXTURETYPE_2D:
    default:
        return SDL_GPU_TEXTURETYPE_2D;
    }
}

static SDL_GPUSampleCount bh_to_sdl_sample_count(BH_GPUSampleCount sc)
{
    switch (sc)
    {
    case BH_GPU_SAMPLECOUNT_1:
    default:
        return SDL_GPU_SAMPLECOUNT_1;
    }
}

static SDL_GPUIndexElementSize bh_to_sdl_index_element_size(BH_GPUIndexElementSize s)
{
    switch (s)
    {
    case BH_GPU_INDEXELEMENTSIZE_32BIT:
        return SDL_GPU_INDEXELEMENTSIZE_32BIT;
    case BH_GPU_INDEXELEMENTSIZE_16BIT:
    default:
        return SDL_GPU_INDEXELEMENTSIZE_16BIT;
    }
}

static SDL_GPUCompareOp bh_to_sdl_compare_op(BH_GPUCompareOp op)
{
    switch (op)
    {
    case BH_GPU_COMPAREOP_NEVER:
        return SDL_GPU_COMPAREOP_NEVER;
    case BH_GPU_COMPAREOP_LESS:
        return SDL_GPU_COMPAREOP_LESS;
    case BH_GPU_COMPAREOP_LESS_OR_EQUAL:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case BH_GPU_COMPAREOP_EQUAL:
        return SDL_GPU_COMPAREOP_EQUAL;
    case BH_GPU_COMPAREOP_GREATER:
        return SDL_GPU_COMPAREOP_GREATER;
    case BH_GPU_COMPAREOP_GREATER_OR_EQUAL:
        return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case BH_GPU_COMPAREOP_NOT_EQUAL:
        return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case BH_GPU_COMPAREOP_ALWAYS:
        return SDL_GPU_COMPAREOP_ALWAYS;
    case BH_GPU_COMPAREOP_INVALID:
    default:
        return SDL_GPU_COMPAREOP_INVALID;
    }
}

static SDL_GPUFilter bh_to_sdl_filter(BH_GPUFilter f)
{
    switch (f)
    {
    case BH_GPU_FILTER_NEAREST:
        return SDL_GPU_FILTER_NEAREST;
    case BH_GPU_FILTER_LINEAR:
    default:
        return SDL_GPU_FILTER_LINEAR;
    }
}

static SDL_GPUSamplerMipmapMode bh_to_sdl_mipmap_mode(BH_GPUSamplerMipmapMode m)
{
    switch (m)
    {
    case BH_GPU_SAMPLERMIPMAPMODE_LINEAR:
        return SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    case BH_GPU_SAMPLERMIPMAPMODE_NEAREST:
    default:
        return SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    }
}

static SDL_GPUSamplerAddressMode bh_to_sdl_address_mode(BH_GPUSamplerAddressMode a)
{
    switch (a)
    {
    case BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE:
        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    case BH_GPU_SAMPLERADDRESSMODE_REPEAT:
    default:
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}

static SDL_GPULoadOp bh_to_sdl_load_op(BH_GPULoadOp op)
{
    switch (op)
    {
    case BH_GPU_LOADOP_LOAD:
        return SDL_GPU_LOADOP_LOAD;
    case BH_GPU_LOADOP_DONT_CARE:
        return SDL_GPU_LOADOP_DONT_CARE;
    case BH_GPU_LOADOP_CLEAR:
    default:
        return SDL_GPU_LOADOP_CLEAR;
    }
}

static SDL_GPUStoreOp bh_to_sdl_store_op(BH_GPUStoreOp op)
{
    switch (op)
    {
    case BH_GPU_STOREOP_DONT_CARE:
        return SDL_GPU_STOREOP_DONT_CARE;
    case BH_GPU_STOREOP_STORE:
    default:
        return SDL_GPU_STOREOP_STORE;
    }
}

static SDL_GPUTextureUsageFlags bh_to_sdl_texture_usage(uint32_t usage)
{
    SDL_GPUTextureUsageFlags out = 0;
    if (usage & BH_GPU_TEXTUREUSAGE_SAMPLER)
        out |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (usage & BH_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
        out |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (usage & BH_GPU_TEXTUREUSAGE_COLOR_TARGET)
        out |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    return out;
}

static SDL_GPUBufferUsageFlags bh_to_sdl_buffer_usage(uint32_t usage)
{
    SDL_GPUBufferUsageFlags out = 0;
    if (usage & BH_GPU_BUFFERUSAGE_VERTEX)
        out |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (usage & BH_GPU_BUFFERUSAGE_INDEX)
        out |= SDL_GPU_BUFFERUSAGE_INDEX;
    return out;
}

static SDL_GPUTransferBufferUsage bh_to_sdl_transfer_usage(uint32_t usage)
{
    SDL_GPUTransferBufferUsage out = 0;
    if (usage & BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD)
        out |= SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    return out;
}

/* -----------------------------------------------------------------------------
   Backend virtual methods
   ----------------------------------------------------------------------------- */

static BH_GPUDevice *bh_sdl_create_device(BH_GPUShaderFormat shader_format, bool debug, const char *driver_name)
{
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(bh_to_sdl_shader_format(shader_format), debug, driver_name);
    return (BH_GPUDevice *)dev;
}

static void bh_sdl_destroy_device(BH_GPUDevice *device)
{
    SDL_DestroyGPUDevice((SDL_GPUDevice *)device);
}

static bool bh_sdl_claim_window(BH_GPUDevice *device, BH_Window *window)
{
    return SDL_ClaimWindowForGPUDevice((SDL_GPUDevice *)device, (SDL_Window *)window);
}

static void bh_sdl_release_window(BH_GPUDevice *device, BH_Window *window)
{
    SDL_ReleaseWindowFromGPUDevice((SDL_GPUDevice *)device, (SDL_Window *)window);
}

static void bh_sdl_set_swapchain_parameters(BH_GPUDevice *device, BH_Window *window, BH_GPUSwapchainComposition comp,
                                            BH_GPUPresentMode present_mode)
{
    SDL_SetGPUSwapchainParameters((SDL_GPUDevice *)device, (SDL_Window *)window, bh_to_sdl_swapchain_comp(comp),
                                  bh_to_sdl_present_mode(present_mode));
}

static BH_GPUTextureFormat bh_sdl_get_swapchain_texture_format(BH_GPUDevice *device, BH_Window *window)
{
    SDL_GPUTextureFormat fmt = SDL_GetGPUSwapchainTextureFormat((SDL_GPUDevice *)device, (SDL_Window *)window);
    return (BH_GPUTextureFormat)fmt;
}

static void bh_sdl_wait_for_idle(BH_GPUDevice *device)
{
    SDL_WaitForGPUIdle((SDL_GPUDevice *)device);
}

static BH_GPUCommandBuffer *bh_sdl_acquire_command_buffer(BH_GPUDevice *device)
{
    return (BH_GPUCommandBuffer *)SDL_AcquireGPUCommandBuffer((SDL_GPUDevice *)device);
}

static void bh_sdl_cancel_command_buffer(BH_GPUCommandBuffer *cmd)
{
    SDL_CancelGPUCommandBuffer((SDL_GPUCommandBuffer *)cmd);
}

static bool bh_sdl_submit_command_buffer(BH_GPUCommandBuffer *cmd)
{
    return SDL_SubmitGPUCommandBuffer((SDL_GPUCommandBuffer *)cmd);
}

static void bh_sdl_wait_and_acquire_swapchain_texture(BH_GPUCommandBuffer *cmd, BH_Window *window,
                                                      BH_GPUTexture **out_texture, uint32_t *out_w, uint32_t *out_h)
{
    SDL_GPUTexture *tex = NULL;
    uint32_t w = 0, h = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture((SDL_GPUCommandBuffer *)cmd, (SDL_Window *)window, &tex, &w, &h);
    if (out_texture)
        *out_texture = (BH_GPUTexture *)tex;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
}

static BH_GPUCopyPass *bh_sdl_begin_copy_pass(BH_GPUCommandBuffer *cmd)
{
    return (BH_GPUCopyPass *)SDL_BeginGPUCopyPass((SDL_GPUCommandBuffer *)cmd);
}

static void bh_sdl_end_copy_pass(BH_GPUCopyPass *copy)
{
    SDL_EndGPUCopyPass((SDL_GPUCopyPass *)copy);
}

static BH_GPURenderPass *bh_sdl_begin_render_pass(BH_GPUCommandBuffer *cmd, const BH_GPUColorTargetInfo *color_targets,
                                                  uint32_t num_color_targets,
                                                  const BH_GPUDepthStencilTargetInfo *depth_target)
{
    SDL_GPUColorTargetInfo stack_targets[8];
    SDL_GPUColorTargetInfo *sdl_targets = stack_targets;
    SDL_GPUDepthStencilTargetInfo sdl_depth;
    SDL_GPUDepthStencilTargetInfo *sdl_depth_ptr = NULL;

    if (num_color_targets > BH_ARRAY_COUNT(stack_targets))
    {
        sdl_targets = (SDL_GPUColorTargetInfo *)SDL_malloc(sizeof(SDL_GPUColorTargetInfo) * num_color_targets);
        if (!sdl_targets)
        {
            return NULL;
        }
    }

    for (uint32_t i = 0; i < num_color_targets; ++i)
    {
        const BH_GPUColorTargetInfo *src = &color_targets[i];
        SDL_GPUColorTargetInfo *dst = &sdl_targets[i];
        *dst = (SDL_GPUColorTargetInfo){0};
        dst->texture = (SDL_GPUTexture *)src->texture;
        dst->clear_color = (SDL_FColor){src->clear_color.r, src->clear_color.g, src->clear_color.b, src->clear_color.a};
        dst->load_op = bh_to_sdl_load_op(src->load_op);
        dst->store_op = bh_to_sdl_store_op(src->store_op);
    }

    if (depth_target && depth_target->texture)
    {
        sdl_depth = (SDL_GPUDepthStencilTargetInfo){0};
        sdl_depth.texture = (SDL_GPUTexture *)depth_target->texture;
        sdl_depth.clear_depth = depth_target->clear_depth;
        sdl_depth.load_op = bh_to_sdl_load_op(depth_target->load_op);
        sdl_depth.store_op = bh_to_sdl_store_op(depth_target->store_op);
        sdl_depth_ptr = &sdl_depth;
    }

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass((SDL_GPUCommandBuffer *)cmd, sdl_targets, num_color_targets,
                                                     sdl_depth_ptr);

    if (sdl_targets != stack_targets)
    {
        SDL_free(sdl_targets);
    }

    return (BH_GPURenderPass *)pass;
}

static void bh_sdl_end_render_pass(BH_GPURenderPass *pass)
{
    SDL_EndGPURenderPass((SDL_GPURenderPass *)pass);
}

static void bh_sdl_set_viewport(BH_GPURenderPass *pass, const BH_GPUViewport *viewport)
{
    SDL_GPUViewport vp = {.x = viewport->x,
                          .y = viewport->y,
                          .w = viewport->w,
                          .h = viewport->h,
                          .min_depth = viewport->min_depth,
                          .max_depth = viewport->max_depth};
    SDL_SetGPUViewport((SDL_GPURenderPass *)pass, &vp);
}

static void bh_sdl_set_scissor(BH_GPURenderPass *pass, const BH_GPU_Rect *rect)
{
    SDL_Rect r = {.x = rect->x, .y = rect->y, .w = rect->w, .h = rect->h};
    SDL_SetGPUScissor((SDL_GPURenderPass *)pass, &r);
}

static void bh_sdl_bind_graphics_pipeline(BH_GPURenderPass *pass, BH_GPUGraphicsPipeline *pipeline)
{
    SDL_BindGPUGraphicsPipeline((SDL_GPURenderPass *)pass, (SDL_GPUGraphicsPipeline *)pipeline);
}

static void bh_sdl_bind_vertex_buffers(BH_GPURenderPass *pass, uint32_t first_slot, const BH_GPUBufferBinding *bindings,
                                       uint32_t num_bindings)
{
    SDL_GPUBufferBinding stack_bindings[8];
    SDL_GPUBufferBinding *sdl_bindings = stack_bindings;

    if (num_bindings > BH_ARRAY_COUNT(stack_bindings))
    {
        sdl_bindings = (SDL_GPUBufferBinding *)SDL_malloc(sizeof(SDL_GPUBufferBinding) * num_bindings);
        if (!sdl_bindings)
            return;
    }

    for (uint32_t i = 0; i < num_bindings; ++i)
    {
        sdl_bindings[i] = (SDL_GPUBufferBinding){.buffer = (SDL_GPUBuffer *)bindings[i].buffer, .offset = bindings[i].offset};
    }

    SDL_BindGPUVertexBuffers((SDL_GPURenderPass *)pass, first_slot, sdl_bindings, num_bindings);

    if (sdl_bindings != stack_bindings)
    {
        SDL_free(sdl_bindings);
    }
}

static void bh_sdl_bind_index_buffer(BH_GPURenderPass *pass, const BH_GPUBufferBinding *binding,
                                     BH_GPUIndexElementSize index_element_size)
{
    SDL_GPUBufferBinding b = {.buffer = (SDL_GPUBuffer *)binding->buffer, .offset = binding->offset};
    SDL_BindGPUIndexBuffer((SDL_GPURenderPass *)pass, &b, bh_to_sdl_index_element_size(index_element_size));
}

static void bh_sdl_bind_fragment_samplers(BH_GPURenderPass *pass, uint32_t first_slot,
                                          const BH_GPUTextureSamplerBinding *bindings, uint32_t num_bindings)
{
    SDL_GPUTextureSamplerBinding stack[8];
    SDL_GPUTextureSamplerBinding *sdl = stack;

    if (num_bindings > BH_ARRAY_COUNT(stack))
    {
        sdl = (SDL_GPUTextureSamplerBinding *)SDL_malloc(sizeof(SDL_GPUTextureSamplerBinding) * num_bindings);
        if (!sdl)
            return;
    }

    for (uint32_t i = 0; i < num_bindings; ++i)
    {
        sdl[i] = (SDL_GPUTextureSamplerBinding){.texture = (SDL_GPUTexture *)bindings[i].texture,
                                                .sampler = (SDL_GPUSampler *)bindings[i].sampler};
    }

    SDL_BindGPUFragmentSamplers((SDL_GPURenderPass *)pass, first_slot, sdl, num_bindings);

    if (sdl != stack)
    {
        SDL_free(sdl);
    }
}

static void bh_sdl_draw_indexed_primitives(BH_GPURenderPass *pass, uint32_t index_count, uint32_t instance_count,
                                           uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    SDL_DrawGPUIndexedPrimitives((SDL_GPURenderPass *)pass, index_count, instance_count, first_index, vertex_offset,
                                 first_instance);
}

static void bh_sdl_draw_primitives(BH_GPURenderPass *pass, uint32_t vertex_count, uint32_t instance_count,
                                   uint32_t first_vertex, uint32_t first_instance)
{
    SDL_DrawGPUPrimitives((SDL_GPURenderPass *)pass, vertex_count, instance_count, first_vertex, first_instance);
}

static BH_GPUTexture *bh_sdl_create_texture(BH_GPUDevice *device, const BH_GPUTextureCreateInfo *info)
{
    SDL_GPUTextureCreateInfo tci = {.type = bh_to_sdl_texture_type(info->type),
                                    .format = (SDL_GPUTextureFormat)info->format,
                                    .usage = bh_to_sdl_texture_usage(info->usage),
                                    .width = info->width,
                                    .height = info->height,
                                    .layer_count_or_depth = info->layer_count_or_depth,
                                    .num_levels = info->num_levels,
                                    .sample_count = bh_to_sdl_sample_count(info->sample_count)};

    return (BH_GPUTexture *)SDL_CreateGPUTexture((SDL_GPUDevice *)device, &tci);
}

static void bh_sdl_release_texture(BH_GPUDevice *device, BH_GPUTexture *texture)
{
    SDL_ReleaseGPUTexture((SDL_GPUDevice *)device, (SDL_GPUTexture *)texture);
}

static BH_GPUBuffer *bh_sdl_create_buffer(BH_GPUDevice *device, const BH_GPUBufferCreateInfo *info)
{
    SDL_GPUBufferCreateInfo bci = {.usage = bh_to_sdl_buffer_usage(info->usage), .size = info->size};
    return (BH_GPUBuffer *)SDL_CreateGPUBuffer((SDL_GPUDevice *)device, &bci);
}

static void bh_sdl_release_buffer(BH_GPUDevice *device, BH_GPUBuffer *buffer)
{
    SDL_ReleaseGPUBuffer((SDL_GPUDevice *)device, (SDL_GPUBuffer *)buffer);
}

static BH_GPUTransferBuffer *bh_sdl_create_transfer_buffer(BH_GPUDevice *device, const BH_GPUTransferBufferCreateInfo *info)
{
    SDL_GPUTransferBufferCreateInfo tci = {.usage = bh_to_sdl_transfer_usage(info->usage), .size = info->size};
    return (BH_GPUTransferBuffer *)SDL_CreateGPUTransferBuffer((SDL_GPUDevice *)device, &tci);
}

static void bh_sdl_release_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    SDL_ReleaseGPUTransferBuffer((SDL_GPUDevice *)device, (SDL_GPUTransferBuffer *)buffer);
}

static void *bh_sdl_map_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer, bool cycle)
{
    return SDL_MapGPUTransferBuffer((SDL_GPUDevice *)device, (SDL_GPUTransferBuffer *)buffer, cycle);
}

static void bh_sdl_unmap_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    SDL_UnmapGPUTransferBuffer((SDL_GPUDevice *)device, (SDL_GPUTransferBuffer *)buffer);
}

static void bh_sdl_upload_to_texture(BH_GPUCopyPass *copy, const BH_GPUTextureTransferInfo *src,
                                     const BH_GPUTextureRegion *dst, bool cycle)
{
    SDL_GPUTextureTransferInfo s = {.transfer_buffer = (SDL_GPUTransferBuffer *)src->transfer_buffer,
                                    .offset = src->offset,
                                    .pixels_per_row = src->pixels_per_row,
                                    .rows_per_layer = src->rows_per_layer};
    SDL_GPUTextureRegion d = {.texture = (SDL_GPUTexture *)dst->texture,
                              .mip_level = dst->mip_level,
                              .layer = dst->layer,
                              .x = dst->x,
                              .y = dst->y,
                              .z = dst->z,
                              .w = dst->w,
                              .h = dst->h,
                              .d = dst->d};
    SDL_UploadToGPUTexture((SDL_GPUCopyPass *)copy, &s, &d, cycle);
}

static void bh_sdl_upload_to_buffer(BH_GPUCopyPass *copy, const BH_GPUTransferBufferLocation *src,
                                    const BH_GPUBufferRegion *dst, bool cycle)
{
    SDL_GPUTransferBufferLocation s = {.transfer_buffer = (SDL_GPUTransferBuffer *)src->transfer_buffer, .offset = src->offset};
    SDL_GPUBufferRegion d = {.buffer = (SDL_GPUBuffer *)dst->buffer, .offset = dst->offset, .size = dst->size};
    SDL_UploadToGPUBuffer((SDL_GPUCopyPass *)copy, &s, &d, cycle);
}

static BH_GPUSampler *bh_sdl_create_sampler(BH_GPUDevice *device, const BH_GPUSamplerCreateInfo *info)
{
    SDL_GPUSamplerCreateInfo sci = {.min_filter = bh_to_sdl_filter(info->min_filter),
                                    .mag_filter = bh_to_sdl_filter(info->mag_filter),
                                    .mipmap_mode = bh_to_sdl_mipmap_mode(info->mipmap_mode),
                                    .address_mode_u = bh_to_sdl_address_mode(info->address_mode_u),
                                    .address_mode_v = bh_to_sdl_address_mode(info->address_mode_v),
                                    .address_mode_w = bh_to_sdl_address_mode(info->address_mode_w),
                                    .min_lod = info->min_lod,
                                    .max_lod = info->max_lod,
                                    .mip_lod_bias = info->mip_lod_bias,
                                    .enable_anisotropy = info->enable_anisotropy,
                                    .max_anisotropy = info->max_anisotropy,
                                    .compare_op = bh_to_sdl_compare_op(info->compare_op)};
    return (BH_GPUSampler *)SDL_CreateGPUSampler((SDL_GPUDevice *)device, &sci);
}

static void bh_sdl_release_sampler(BH_GPUDevice *device, BH_GPUSampler *sampler)
{
    SDL_ReleaseGPUSampler((SDL_GPUDevice *)device, (SDL_GPUSampler *)sampler);
}

static BH_GPUShader *bh_sdl_create_shader(BH_GPUDevice *device, const BH_GPUShaderCreateInfo *info)
{
    SDL_GPUShaderCreateInfo ci = {0};
    ci.code = info->code;
    ci.code_size = info->code_size;
    ci.entrypoint = info->entrypoint;
    ci.format = bh_to_sdl_shader_format(info->format);
    ci.stage = bh_to_sdl_shader_stage(info->stage);
    ci.num_samplers = info->num_samplers;
    ci.num_storage_textures = info->num_storage_textures;
    ci.num_storage_buffers = info->num_storage_buffers;
    ci.num_uniform_buffers = info->num_uniform_buffers;

    return (BH_GPUShader *)SDL_CreateGPUShader((SDL_GPUDevice *)device, &ci);
}

static void bh_sdl_release_shader(BH_GPUDevice *device, BH_GPUShader *shader)
{
    SDL_ReleaseGPUShader((SDL_GPUDevice *)device, (SDL_GPUShader *)shader);
}

static BH_GPUGraphicsPipeline *bh_sdl_create_graphics_pipeline(BH_GPUDevice *device, const BH_GPUGraphicsPipelineCreateInfo *info)
{
    SDL_GPUVertexBufferDescription stack_vbs[8];
    SDL_GPUVertexAttribute stack_attrs[16];

    SDL_GPUVertexBufferDescription *vbs = stack_vbs;
    SDL_GPUVertexAttribute *attrs = stack_attrs;

    const uint32_t num_vbs = info->vertex_input_state.num_vertex_buffers;
    const uint32_t num_attrs = info->vertex_input_state.num_vertex_attributes;

    if (num_vbs > BH_ARRAY_COUNT(stack_vbs))
    {
        vbs = (SDL_GPUVertexBufferDescription *)SDL_malloc(sizeof(SDL_GPUVertexBufferDescription) * num_vbs);
        if (!vbs)
            return NULL;
    }
    if (num_attrs > BH_ARRAY_COUNT(stack_attrs))
    {
        attrs = (SDL_GPUVertexAttribute *)SDL_malloc(sizeof(SDL_GPUVertexAttribute) * num_attrs);
        if (!attrs)
        {
            if (vbs != stack_vbs)
                SDL_free(vbs);
            return NULL;
        }
    }

    for (uint32_t i = 0; i < num_vbs; ++i)
    {
        const BH_GPUVertexBufferDescription *src = &info->vertex_input_state.vertex_buffer_descriptions[i];
        SDL_GPUVertexBufferDescription *dst = &vbs[i];
        *dst = (SDL_GPUVertexBufferDescription){0};
        dst->slot = src->slot;
        dst->pitch = src->pitch;
        dst->input_rate = bh_to_sdl_vertex_input_rate(src->input_rate);
        dst->instance_step_rate = src->instance_step_rate;
    }

    for (uint32_t i = 0; i < num_attrs; ++i)
    {
        const BH_GPUVertexAttribute *src = &info->vertex_input_state.vertex_attributes[i];
        SDL_GPUVertexAttribute *dst = &attrs[i];
        *dst = (SDL_GPUVertexAttribute){0};
        dst->location = src->location;
        dst->buffer_slot = src->buffer_slot;
        dst->format = bh_to_sdl_vertex_element_format(src->format);
        dst->offset = src->offset;
    }

    SDL_GPUVertexInputState vi = {0};
    vi.vertex_buffer_descriptions = (num_vbs > 0) ? vbs : NULL;
    vi.num_vertex_buffers = num_vbs;
    vi.vertex_attributes = (num_attrs > 0) ? attrs : NULL;
    vi.num_vertex_attributes = num_attrs;

    SDL_GPURasterizerState rast = {0};
    rast.fill_mode = bh_to_sdl_fill_mode(info->rasterizer_state.fill_mode);
    rast.cull_mode = bh_to_sdl_cull_mode(info->rasterizer_state.cull_mode);
    rast.front_face = bh_to_sdl_front_face(info->rasterizer_state.front_face);

    SDL_GPUMultisampleState ms = {0};
    ms.sample_count = bh_to_sdl_sample_count(info->multisample_state.sample_count);

    SDL_GPUDepthStencilState ds = {0};
    ds.enable_depth_test = info->depth_stencil_state.enable_depth_test;
    ds.enable_depth_write = info->depth_stencil_state.enable_depth_write;
    ds.compare_op = bh_to_sdl_compare_op(info->depth_stencil_state.compare_op);

    SDL_GPUColorTargetDescription stack_colors[8];
    SDL_GPUColorTargetDescription *colors = stack_colors;
    const uint32_t num_colors = info->target_info.num_color_targets;

    if (num_colors > BH_ARRAY_COUNT(stack_colors))
    {
        colors = (SDL_GPUColorTargetDescription *)SDL_malloc(sizeof(SDL_GPUColorTargetDescription) * num_colors);
        if (!colors)
        {
            if (attrs != stack_attrs)
                SDL_free(attrs);
            if (vbs != stack_vbs)
                SDL_free(vbs);
            return NULL;
        }
    }

    for (uint32_t i = 0; i < num_colors; ++i)
    {
        const BH_GPUColorTargetDescription *src = &info->target_info.color_target_descriptions[i];
        SDL_GPUColorTargetDescription *dst = &colors[i];
        *dst = (SDL_GPUColorTargetDescription){0};
        dst->format = (SDL_GPUTextureFormat)src->format;

        const BH_GPUColorTargetBlendState *bst = &src->blend_state;
        dst->blend_state.enable_blend = bst->enable_blend;
        dst->blend_state.color_blend_op = bh_to_sdl_blend_op(bst->color_blend_op);
        dst->blend_state.alpha_blend_op = bh_to_sdl_blend_op(bst->alpha_blend_op);
        dst->blend_state.src_color_blendfactor = bh_to_sdl_blend_factor(bst->src_color_blendfactor);
        dst->blend_state.dst_color_blendfactor = bh_to_sdl_blend_factor(bst->dst_color_blendfactor);
        dst->blend_state.src_alpha_blendfactor = bh_to_sdl_blend_factor(bst->src_alpha_blendfactor);
        dst->blend_state.dst_alpha_blendfactor = bh_to_sdl_blend_factor(bst->dst_alpha_blendfactor);
        dst->blend_state.enable_color_write_mask = bst->enable_color_write_mask;
    }

    SDL_GPUGraphicsPipelineCreateInfo pci = {0};
    pci.vertex_shader = (SDL_GPUShader *)info->vertex_shader;
    pci.fragment_shader = (SDL_GPUShader *)info->fragment_shader;
    pci.vertex_input_state = vi;
    pci.primitive_type = bh_to_sdl_primitive_type(info->primitive_type);
    pci.rasterizer_state = rast;
    pci.multisample_state = ms;
    pci.depth_stencil_state = ds;

    pci.target_info.color_target_descriptions = (num_colors > 0) ? colors : NULL;
    pci.target_info.num_color_targets = num_colors;
    pci.target_info.depth_stencil_format = (SDL_GPUTextureFormat)info->target_info.depth_stencil_format;
    pci.target_info.has_depth_stencil_target = info->target_info.has_depth_stencil_target;

    SDL_GPUGraphicsPipeline *pipe = SDL_CreateGPUGraphicsPipeline((SDL_GPUDevice *)device, &pci);

    if (colors != stack_colors)
        SDL_free(colors);
    if (attrs != stack_attrs)
        SDL_free(attrs);
    if (vbs != stack_vbs)
        SDL_free(vbs);

    return (BH_GPUGraphicsPipeline *)pipe;
}

static void bh_sdl_release_graphics_pipeline(BH_GPUDevice *device, BH_GPUGraphicsPipeline *pipeline)
{
    SDL_ReleaseGPUGraphicsPipeline((SDL_GPUDevice *)device, (SDL_GPUGraphicsPipeline *)pipeline);
}

static void bh_sdl_push_vertex_uniform_data(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    SDL_PushGPUVertexUniformData((SDL_GPUCommandBuffer *)cmd, slot, data, size);
}

static void bh_sdl_push_fragment_uniform_data(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    SDL_PushGPUFragmentUniformData((SDL_GPUCommandBuffer *)cmd, slot, data, size);
}

static const char *bh_sdl_get_last_error(void)
{
    return SDL_GetError();
}

static BH_GPUTextureFormat bh_sdl_fmt_r8g8b8a8_unorm(void)
{
    return (BH_GPUTextureFormat)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

static BH_GPUTextureFormat bh_sdl_fmt_r8g8b8a8_unorm_srgb(void)
{
    return (BH_GPUTextureFormat)SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
}

static BH_GPUTextureFormat bh_sdl_fmt_d32_float(void)
{
    return (BH_GPUTextureFormat)SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
}

/* -----------------------------------------------------------------------------
   Backend descriptor
   ----------------------------------------------------------------------------- */

static const BH_GPUBackendVTable g_sdl_vtable = {
    .create_device = bh_sdl_create_device,
    .destroy_device = bh_sdl_destroy_device,

    .claim_window = bh_sdl_claim_window,
    .release_window = bh_sdl_release_window,

    .set_swapchain_parameters = bh_sdl_set_swapchain_parameters,
    .get_swapchain_texture_format = bh_sdl_get_swapchain_texture_format,

    .wait_for_idle = bh_sdl_wait_for_idle,

    .acquire_command_buffer = bh_sdl_acquire_command_buffer,
    .cancel_command_buffer = bh_sdl_cancel_command_buffer,
    .submit_command_buffer = bh_sdl_submit_command_buffer,

    .wait_and_acquire_swapchain_texture = bh_sdl_wait_and_acquire_swapchain_texture,

    .begin_copy_pass = bh_sdl_begin_copy_pass,
    .end_copy_pass = bh_sdl_end_copy_pass,

    .begin_render_pass = bh_sdl_begin_render_pass,
    .end_render_pass = bh_sdl_end_render_pass,

    .set_viewport = bh_sdl_set_viewport,
    .set_scissor = bh_sdl_set_scissor,

    .bind_graphics_pipeline = bh_sdl_bind_graphics_pipeline,
    .bind_vertex_buffers = bh_sdl_bind_vertex_buffers,
    .bind_index_buffer = bh_sdl_bind_index_buffer,
    .bind_fragment_samplers = bh_sdl_bind_fragment_samplers,
    .draw_primitives = bh_sdl_draw_primitives,
    .draw_indexed_primitives = bh_sdl_draw_indexed_primitives,

    .create_texture = bh_sdl_create_texture,
    .release_texture = bh_sdl_release_texture,

    .create_buffer = bh_sdl_create_buffer,
    .release_buffer = bh_sdl_release_buffer,

    .create_transfer_buffer = bh_sdl_create_transfer_buffer,
    .release_transfer_buffer = bh_sdl_release_transfer_buffer,
    .map_transfer_buffer = bh_sdl_map_transfer_buffer,
    .unmap_transfer_buffer = bh_sdl_unmap_transfer_buffer,
    .upload_to_texture = bh_sdl_upload_to_texture,
    .upload_to_buffer = bh_sdl_upload_to_buffer,

    .create_sampler = bh_sdl_create_sampler,
    .release_sampler = bh_sdl_release_sampler,

    .create_shader = bh_sdl_create_shader,
    .release_shader = bh_sdl_release_shader,

    .create_graphics_pipeline = bh_sdl_create_graphics_pipeline,
    .release_graphics_pipeline = bh_sdl_release_graphics_pipeline,

    .push_vertex_uniform_data = bh_sdl_push_vertex_uniform_data,
    .push_fragment_uniform_data = bh_sdl_push_fragment_uniform_data,

    .get_last_error = bh_sdl_get_last_error,

    .get_texture_format_r8g8b8a8_unorm = bh_sdl_fmt_r8g8b8a8_unorm,
    .get_texture_format_r8g8b8a8_unorm_srgb = bh_sdl_fmt_r8g8b8a8_unorm_srgb,
    .get_texture_format_d32_float = bh_sdl_fmt_d32_float,
};

const BH_GPUBackend BH_GPU_BACKEND_SDL = {
    .name = "SDL3",
    .vt = &g_sdl_vtable,
};
