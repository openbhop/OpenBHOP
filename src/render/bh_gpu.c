/* -----------------------------------------------------------------------------
   bh_gpu.c
   ----------------------------------------------------------------------------- */

#include "bh_gpu.h"

/* Default backend is SDL3's SDL_gpu wrapper. */
extern const BH_GPUBackend BH_GPU_BACKEND_SDL;

static const BH_GPUBackend *g_backend = &BH_GPU_BACKEND_SDL;

const BH_GPUBackend *BH_GPU_GetBackend(void)
{
    return g_backend;
}

void BH_GPU_SetBackend(const BH_GPUBackend *backend)
{
    if (backend && backend->vt)
    {
        g_backend = backend;
    }
}

/* -----------------------------------------------------------------------------
   Convenience wrappers
   ----------------------------------------------------------------------------- */

BH_GPUDevice *BH_GPU_CreateDevice(BH_GPUShaderFormat shader_format, bool debug, const char *driver_name)
{
    return g_backend->vt->create_device(shader_format, debug, driver_name);
}

void BH_GPU_DestroyDevice(BH_GPUDevice *device)
{
    if (device)
    {
        g_backend->vt->destroy_device(device);
    }
}

bool BH_GPU_ClaimWindowForDevice(BH_GPUDevice *device, BH_Window *window)
{
    return (device && window) ? g_backend->vt->claim_window(device, window) : false;
}

void BH_GPU_ReleaseWindowFromDevice(BH_GPUDevice *device, BH_Window *window)
{
    if (device && window)
    {
        g_backend->vt->release_window(device, window);
    }
}

void BH_GPU_SetSwapchainParameters(BH_GPUDevice *device, BH_Window *window, BH_GPUSwapchainComposition comp,
                                   BH_GPUPresentMode present_mode)
{
    if (device && window)
    {
        g_backend->vt->set_swapchain_parameters(device, window, comp, present_mode);
    }
}

BH_GPUTextureFormat BH_GPU_GetSwapchainTextureFormat(BH_GPUDevice *device, BH_Window *window)
{
    return (device && window) ? g_backend->vt->get_swapchain_texture_format(device, window) : 0u;
}

void BH_GPU_WaitForIdle(BH_GPUDevice *device)
{
    if (device)
    {
        g_backend->vt->wait_for_idle(device);
    }
}

BH_GPUCommandBuffer *BH_GPU_AcquireCommandBuffer(BH_GPUDevice *device)
{
    return device ? g_backend->vt->acquire_command_buffer(device) : NULL;
}

void BH_GPU_CancelCommandBuffer(BH_GPUCommandBuffer *cmd)
{
    if (cmd)
    {
        g_backend->vt->cancel_command_buffer(cmd);
    }
}

bool BH_GPU_SubmitCommandBuffer(BH_GPUCommandBuffer *cmd)
{
    return cmd ? g_backend->vt->submit_command_buffer(cmd) : false;
}

void BH_GPU_WaitAndAcquireSwapchainTexture(BH_GPUCommandBuffer *cmd, BH_Window *window, BH_GPUTexture **out_texture,
                                           uint32_t *out_w, uint32_t *out_h)
{
    if (cmd && window)
    {
        g_backend->vt->wait_and_acquire_swapchain_texture(cmd, window, out_texture, out_w, out_h);
    }
    else
    {
        if (out_texture)
            *out_texture = NULL;
        if (out_w)
            *out_w = 0;
        if (out_h)
            *out_h = 0;
    }
}

BH_GPUCopyPass *BH_GPU_BeginCopyPass(BH_GPUCommandBuffer *cmd)
{
    return cmd ? g_backend->vt->begin_copy_pass(cmd) : NULL;
}

void BH_GPU_EndCopyPass(BH_GPUCopyPass *copy)
{
    if (copy)
    {
        g_backend->vt->end_copy_pass(copy);
    }
}

BH_GPURenderPass *BH_GPU_BeginRenderPass(BH_GPUCommandBuffer *cmd, const BH_GPUColorTargetInfo *color_targets,
                                         uint32_t num_color_targets,
                                         const BH_GPUDepthStencilTargetInfo *depth_target)
{
    return cmd ? g_backend->vt->begin_render_pass(cmd, color_targets, num_color_targets, depth_target) : NULL;
}

void BH_GPU_EndRenderPass(BH_GPURenderPass *pass)
{
    if (pass)
    {
        g_backend->vt->end_render_pass(pass);
    }
}

void BH_GPU_SetViewport(BH_GPURenderPass *pass, const BH_GPUViewport *viewport)
{
    if (pass && viewport)
    {
        g_backend->vt->set_viewport(pass, viewport);
    }
}

void BH_GPU_SetScissor(BH_GPURenderPass *pass, const BH_GPU_Rect *rect)
{
    if (pass && rect)
    {
        g_backend->vt->set_scissor(pass, rect);
    }
}

void BH_GPU_BindGraphicsPipeline(BH_GPURenderPass *pass, BH_GPUGraphicsPipeline *pipeline)
{
    if (pass && pipeline)
    {
        g_backend->vt->bind_graphics_pipeline(pass, pipeline);
    }
}

void BH_GPU_BindVertexBuffers(BH_GPURenderPass *pass, uint32_t first_slot, const BH_GPUBufferBinding *bindings,
                              uint32_t num_bindings)
{
    if (pass && bindings && num_bindings > 0)
    {
        g_backend->vt->bind_vertex_buffers(pass, first_slot, bindings, num_bindings);
    }
}

void BH_GPU_BindIndexBuffer(BH_GPURenderPass *pass, const BH_GPUBufferBinding *binding,
                            BH_GPUIndexElementSize index_element_size)
{
    if (pass && binding)
    {
        g_backend->vt->bind_index_buffer(pass, binding, index_element_size);
    }
}

void BH_GPU_BindFragmentSamplers(BH_GPURenderPass *pass, uint32_t first_slot,
                                 const BH_GPUTextureSamplerBinding *bindings, uint32_t num_bindings)
{
    if (pass && bindings && num_bindings > 0)
    {
        g_backend->vt->bind_fragment_samplers(pass, first_slot, bindings, num_bindings);
    }
}

void BH_GPU_DrawPrimitives(BH_GPURenderPass *pass, uint32_t vertex_count, uint32_t instance_count,
                           uint32_t first_vertex, uint32_t first_instance)
{
    if (pass && g_backend->vt->draw_primitives)
    {
        g_backend->vt->draw_primitives(pass, vertex_count, instance_count, first_vertex, first_instance);
    }
}

void BH_GPU_DrawIndexedPrimitives(BH_GPURenderPass *pass, uint32_t index_count, uint32_t instance_count,
                                  uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    if (pass)
    {
        g_backend->vt->draw_indexed_primitives(pass, index_count, instance_count, first_index, vertex_offset,
                                               first_instance);
    }
}

BH_GPUTexture *BH_GPU_CreateTexture(BH_GPUDevice *device, const BH_GPUTextureCreateInfo *info)
{
    return (device && info) ? g_backend->vt->create_texture(device, info) : NULL;
}

void BH_GPU_ReleaseTexture(BH_GPUDevice *device, BH_GPUTexture *texture)
{
    if (device && texture)
    {
        g_backend->vt->release_texture(device, texture);
    }
}

BH_GPUBuffer *BH_GPU_CreateBuffer(BH_GPUDevice *device, const BH_GPUBufferCreateInfo *info)
{
    return (device && info) ? g_backend->vt->create_buffer(device, info) : NULL;
}

void BH_GPU_ReleaseBuffer(BH_GPUDevice *device, BH_GPUBuffer *buffer)
{
    if (device && buffer)
    {
        g_backend->vt->release_buffer(device, buffer);
    }
}

BH_GPUTransferBuffer *BH_GPU_CreateTransferBuffer(BH_GPUDevice *device, const BH_GPUTransferBufferCreateInfo *info)
{
    return (device && info) ? g_backend->vt->create_transfer_buffer(device, info) : NULL;
}

void BH_GPU_ReleaseTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    if (device && buffer)
    {
        g_backend->vt->release_transfer_buffer(device, buffer);
    }
}

void *BH_GPU_MapTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer, bool cycle)
{
    return (device && buffer) ? g_backend->vt->map_transfer_buffer(device, buffer, cycle) : NULL;
}

void BH_GPU_UnmapTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    if (device && buffer)
    {
        g_backend->vt->unmap_transfer_buffer(device, buffer);
    }
}

void BH_GPU_UploadToTexture(BH_GPUCopyPass *copy, const BH_GPUTextureTransferInfo *src,
                            const BH_GPUTextureRegion *dst, bool cycle)
{
    if (copy && src && dst)
    {
        g_backend->vt->upload_to_texture(copy, src, dst, cycle);
    }
}

void BH_GPU_UploadToBuffer(BH_GPUCopyPass *copy, const BH_GPUTransferBufferLocation *src, const BH_GPUBufferRegion *dst,
                           bool cycle)
{
    if (copy && src && dst)
    {
        g_backend->vt->upload_to_buffer(copy, src, dst, cycle);
    }
}

BH_GPUSampler *BH_GPU_CreateSampler(BH_GPUDevice *device, const BH_GPUSamplerCreateInfo *info)
{
    return (device && info) ? g_backend->vt->create_sampler(device, info) : NULL;
}

void BH_GPU_ReleaseSampler(BH_GPUDevice *device, BH_GPUSampler *sampler)
{
    if (device && sampler)
    {
        g_backend->vt->release_sampler(device, sampler);
    }
}

BH_GPUShader *BH_GPU_CreateShader(BH_GPUDevice *device, const BH_GPUShaderCreateInfo *info)
{
    return (device && info && g_backend->vt->create_shader) ? g_backend->vt->create_shader(device, info) : NULL;
}

void BH_GPU_ReleaseShader(BH_GPUDevice *device, BH_GPUShader *shader)
{
    if (device && shader && g_backend->vt->release_shader)
    {
        g_backend->vt->release_shader(device, shader);
    }
}

BH_GPUGraphicsPipeline *BH_GPU_CreateGraphicsPipeline(BH_GPUDevice *device, const BH_GPUGraphicsPipelineCreateInfo *info)
{
    return (device && info && g_backend->vt->create_graphics_pipeline) ? g_backend->vt->create_graphics_pipeline(device, info)
                                                                      : NULL;
}

void BH_GPU_ReleaseGraphicsPipeline(BH_GPUDevice *device, BH_GPUGraphicsPipeline *pipeline)
{
    if (device && pipeline && g_backend->vt->release_graphics_pipeline)
    {
        g_backend->vt->release_graphics_pipeline(device, pipeline);
    }
}

void BH_GPU_PushVertexUniformData(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    if (cmd && data && size && g_backend->vt->push_vertex_uniform_data)
    {
        g_backend->vt->push_vertex_uniform_data(cmd, slot, data, size);
    }
}

void BH_GPU_PushFragmentUniformData(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    if (cmd && data && size && g_backend->vt->push_fragment_uniform_data)
    {
        g_backend->vt->push_fragment_uniform_data(cmd, slot, data, size);
    }
}

const char *BH_GPU_GetLastError(void)
{
    return g_backend->vt->get_last_error ? g_backend->vt->get_last_error() : "";
}

BH_GPUTextureFormat BH_GPU_GetTextureFormat_R8G8B8A8_UNORM(void)
{
    return g_backend->vt->get_texture_format_r8g8b8a8_unorm ? g_backend->vt->get_texture_format_r8g8b8a8_unorm() : 0u;
}

BH_GPUTextureFormat BH_GPU_GetTextureFormat_R8G8B8A8_UNORM_SRGB(void)
{
    return g_backend->vt->get_texture_format_r8g8b8a8_unorm_srgb ? g_backend->vt->get_texture_format_r8g8b8a8_unorm_srgb()
                                                                 : 0u;
}

BH_GPUTextureFormat BH_GPU_GetTextureFormat_D32_FLOAT(void)
{
    return g_backend->vt->get_texture_format_d32_float ? g_backend->vt->get_texture_format_d32_float() : 0u;
}
