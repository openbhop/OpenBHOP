/* -----------------------------------------------------------------------------
   bh_gpu.h
   -----------------------------------------------------------------------------

   Minimal graphics abstraction layer.

   Notes
   - This header intentionally avoids including any backend-specific GPU headers.
   - Handles are opaque (declared as void) to keep all higher-level systems
     backend-agnostic.
   - A backend is selected via a vtable (C11-style OOP).
*/

#pragma once

#include "../core/bh_core.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* -------------------------------------------------------------------------
       Opaque Handle Types
       ------------------------------------------------------------------------- */

    typedef void BH_GPUDevice;
    typedef void BH_GPUCommandBuffer;
    typedef void BH_GPUCopyPass;
    typedef void BH_GPURenderPass;

    typedef void BH_GPUTexture;
    typedef void BH_GPUBuffer;
    typedef void BH_GPUTransferBuffer;
    typedef void BH_GPUSampler;

    /* Some subsystems still create backend-native pipelines/shaders directly.
       Keep these opaque so renderer code can bind them without knowing details. */
    typedef void BH_GPUShader;
    typedef void BH_GPUGraphicsPipeline;

    /* Generic window handle (SDL_Window*, GLFWwindow*, etc). */
    typedef void BH_Window;

    /* Backend-native texture format token.
       - Treat as an opaque integer.
       - Obtained from BH_GPU_GetSwapchainTextureFormat or BH_GPU_GetTextureFormat_* helpers.
    */
    typedef uint32_t BH_GPUTextureFormat;

    /* -------------------------------------------------------------------------
       Enums / Flags (backend-agnostic)
       ------------------------------------------------------------------------- */

    typedef enum BH_GPUShaderFormat
    {
        BH_GPU_SHADERFORMAT_SPIRV = 1,
        BH_GPU_SHADERFORMAT_GLSL = 2,
    } BH_GPUShaderFormat;

    typedef enum BH_GPUSwapchainComposition
    {
        BH_GPU_SWAPCHAINCOMPOSITION_SDR = 1,
    } BH_GPUSwapchainComposition;

    typedef enum BH_GPUPresentMode
    {
        BH_GPU_PRESENTMODE_IMMEDIATE = 1,
    } BH_GPUPresentMode;

    typedef enum BH_GPUTextureType
    {
        BH_GPU_TEXTURETYPE_2D = 1,
    } BH_GPUTextureType;

    typedef enum BH_GPUSampleCount
    {
        BH_GPU_SAMPLECOUNT_1 = 1,
    } BH_GPUSampleCount;

    typedef enum BH_GPUIndexElementSize
    {
        BH_GPU_INDEXELEMENTSIZE_16BIT = 2,
        BH_GPU_INDEXELEMENTSIZE_32BIT = 4,
    } BH_GPUIndexElementSize;

    typedef enum BH_GPUShaderStage
    {
        BH_GPU_SHADERSTAGE_VERTEX = 1,
        BH_GPU_SHADERSTAGE_FRAGMENT = 2,
    } BH_GPUShaderStage;

    typedef enum BH_GPUVertexElementFormat
    {
        BH_GPU_VERTEXELEMENTFORMAT_INVALID = 0,
        BH_GPU_VERTEXELEMENTFORMAT_FLOAT2 = 1,
        BH_GPU_VERTEXELEMENTFORMAT_FLOAT3 = 2,
        BH_GPU_VERTEXELEMENTFORMAT_FLOAT4 = 3,
        BH_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM = 4,
    } BH_GPUVertexElementFormat;

    typedef enum BH_GPUVertexInputRate
    {
        BH_GPU_VERTEXINPUTRATE_VERTEX = 1,
        BH_GPU_VERTEXINPUTRATE_INSTANCE = 2,
    } BH_GPUVertexInputRate;

    typedef enum BH_GPUPrimitiveType
    {
        BH_GPU_PRIMITIVETYPE_TRIANGLELIST = 1,
        BH_GPU_PRIMITIVETYPE_LINELIST = 2,
    } BH_GPUPrimitiveType;

    typedef enum BH_GPUFillMode
    {
        BH_GPU_FILLMODE_FILL = 1,
    } BH_GPUFillMode;

    typedef enum BH_GPUCullMode
    {
        BH_GPU_CULLMODE_NONE = 0,
        BH_GPU_CULLMODE_FRONT = 1,
        BH_GPU_CULLMODE_BACK = 2,
    } BH_GPUCullMode;

    typedef enum BH_GPUFrontFace
    {
        BH_GPU_FRONTFACE_COUNTER_CLOCKWISE = 1,
        BH_GPU_FRONTFACE_CLOCKWISE = 2,
    } BH_GPUFrontFace;

    typedef enum BH_GPUCompareOp
    {
        BH_GPU_COMPAREOP_INVALID = 0,
        BH_GPU_COMPAREOP_NEVER = 1,
        BH_GPU_COMPAREOP_LESS = 2,
        BH_GPU_COMPAREOP_LESS_OR_EQUAL = 3,
        BH_GPU_COMPAREOP_EQUAL = 4,
        BH_GPU_COMPAREOP_GREATER = 5,
        BH_GPU_COMPAREOP_GREATER_OR_EQUAL = 6,
        BH_GPU_COMPAREOP_NOT_EQUAL = 7,
        BH_GPU_COMPAREOP_ALWAYS = 8,
    } BH_GPUCompareOp;

    typedef enum BH_GPUBlendFactor
    {
        BH_GPU_BLENDFACTOR_ZERO = 0,
        BH_GPU_BLENDFACTOR_ONE = 1,
        BH_GPU_BLENDFACTOR_SRC_ALPHA = 2,
        BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA = 3,
    } BH_GPUBlendFactor;

    typedef enum BH_GPUBlendOp
    {
        BH_GPU_BLENDOP_ADD = 1,
    } BH_GPUBlendOp;

    typedef enum BH_GPUFilter
    {
        BH_GPU_FILTER_NEAREST = 1,
        BH_GPU_FILTER_LINEAR = 2,
    } BH_GPUFilter;

    typedef enum BH_GPUSamplerMipmapMode
    {
        BH_GPU_SAMPLERMIPMAPMODE_NEAREST = 1,
        BH_GPU_SAMPLERMIPMAPMODE_LINEAR = 2,
    } BH_GPUSamplerMipmapMode;

    typedef enum BH_GPUSamplerAddressMode
    {
        BH_GPU_SAMPLERADDRESSMODE_REPEAT = 1,
        BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE = 2,
    } BH_GPUSamplerAddressMode;

    typedef enum BH_GPULoadOp
    {
        BH_GPU_LOADOP_LOAD = 1,
        BH_GPU_LOADOP_CLEAR = 2,
        BH_GPU_LOADOP_DONT_CARE = 3,
    } BH_GPULoadOp;

    typedef enum BH_GPUStoreOp
    {
        BH_GPU_STOREOP_STORE = 1,
        BH_GPU_STOREOP_DONT_CARE = 2,
    } BH_GPUStoreOp;

    typedef enum BH_GPUTextureUsageFlags
    {
        BH_GPU_TEXTUREUSAGE_SAMPLER = 1u << 0,
        BH_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET = 1u << 1,
        BH_GPU_TEXTUREUSAGE_COLOR_TARGET = 1u << 2,
    } BH_GPUTextureUsageFlags;

    typedef enum BH_GPUBufferUsageFlags
    {
        BH_GPU_BUFFERUSAGE_VERTEX = 1u << 0,
        BH_GPU_BUFFERUSAGE_INDEX = 1u << 1,
        BH_GPU_BUFFERUSAGE_DYNAMIC = 1u << 2,
    } BH_GPUBufferUsageFlags;

    typedef enum BH_GPUTransferBufferUsage
    {
        BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD = 1u << 0,
    } BH_GPUTransferBufferUsage;

    /* -------------------------------------------------------------------------
       POD structs used by the renderer and asset uploaders.
       ------------------------------------------------------------------------- */

    typedef struct BH_GPUColor
    {
        float r, g, b, a;
    } BH_GPUColor;

    typedef struct BH_GPUViewport
    {
        float x, y, w, h;
        float min_depth, max_depth;
    } BH_GPUViewport;

    typedef struct BH_GPU_Rect
    {
        int x, y, w, h;
    } BH_GPU_Rect;

    typedef struct BH_GPUTextureCreateInfo
    {
        BH_GPUTextureType type;
        BH_GPUTextureFormat format;
        uint32_t usage; /* BH_GPUTextureUsageFlags bitmask */
        uint32_t width;
        uint32_t height;
        uint32_t layer_count_or_depth;
        uint32_t num_levels;
        BH_GPUSampleCount sample_count;
    } BH_GPUTextureCreateInfo;

    typedef struct BH_GPUBufferCreateInfo
    {
        uint32_t usage; /* BH_GPUBufferUsageFlags bitmask */
        uint32_t size;
    } BH_GPUBufferCreateInfo;

    typedef struct BH_GPUTransferBufferCreateInfo
    {
        uint32_t usage; /* BH_GPUTransferBufferUsage bitmask */
        uint32_t size;
    } BH_GPUTransferBufferCreateInfo;

    typedef struct BH_GPUSamplerCreateInfo
    {
        BH_GPUFilter min_filter;
        BH_GPUFilter mag_filter;
        BH_GPUSamplerMipmapMode mipmap_mode;

        BH_GPUSamplerAddressMode address_mode_u;
        BH_GPUSamplerAddressMode address_mode_v;
        BH_GPUSamplerAddressMode address_mode_w;

        float min_lod;
        float max_lod;
        float mip_lod_bias;

        bool enable_anisotropy;
        float max_anisotropy;

        BH_GPUCompareOp compare_op;
    } BH_GPUSamplerCreateInfo;

    typedef struct BH_GPUTextureTransferInfo
    {
        BH_GPUTransferBuffer *transfer_buffer;
        uint32_t offset;
        uint32_t pixels_per_row;
        uint32_t rows_per_layer;
    } BH_GPUTextureTransferInfo;

    typedef struct BH_GPUTextureRegion
    {
        BH_GPUTexture *texture;
        uint32_t mip_level;
        uint32_t layer;
        uint32_t x, y, z;
        uint32_t w, h, d;
    } BH_GPUTextureRegion;

    typedef struct BH_GPUTransferBufferLocation
    {
        BH_GPUTransferBuffer *transfer_buffer;
        uint32_t offset;
    } BH_GPUTransferBufferLocation;

    typedef struct BH_GPUBufferRegion
    {
        BH_GPUBuffer *buffer;
        uint32_t offset;
        uint32_t size;
    } BH_GPUBufferRegion;

    typedef struct BH_GPUTextureSamplerBinding
    {
        BH_GPUTexture *texture;
        BH_GPUSampler *sampler;
    } BH_GPUTextureSamplerBinding;

    typedef struct BH_GPUBufferBinding
    {
        BH_GPUBuffer *buffer;
        uint32_t offset;
    } BH_GPUBufferBinding;

    typedef struct BH_GPUColorTargetInfo
    {
        BH_GPUTexture *texture;
        BH_GPUColor clear_color;
        BH_GPULoadOp load_op;
        BH_GPUStoreOp store_op;
    } BH_GPUColorTargetInfo;

    typedef struct BH_GPUDepthStencilTargetInfo
    {
        BH_GPUTexture *texture;
        float clear_depth;
        BH_GPULoadOp load_op;
        BH_GPUStoreOp store_op;
    } BH_GPUDepthStencilTargetInfo;

    /* -------------------------------------------------------------------------
       Pipeline / Shader resource descriptions
       ------------------------------------------------------------------------- */

    typedef struct BH_GPUShaderCreateInfo
    {
        const void *code;
        size_t code_size;
        const char *entrypoint;
        BH_GPUShaderFormat format;
        BH_GPUShaderStage stage;

        /* Reflection-derived resource counts (backend may require these). */
        uint32_t num_samplers;
        uint32_t num_storage_textures;
        uint32_t num_storage_buffers;
        uint32_t num_uniform_buffers;
    } BH_GPUShaderCreateInfo;

    typedef struct BH_GPUVertexAttribute
    {
        uint32_t location;
        uint32_t buffer_slot;
        BH_GPUVertexElementFormat format;
        uint32_t offset;
    } BH_GPUVertexAttribute;

    typedef struct BH_GPUVertexBufferDescription
    {
        uint32_t slot;
        uint32_t pitch;
        BH_GPUVertexInputRate input_rate;
        uint32_t instance_step_rate;
    } BH_GPUVertexBufferDescription;

    typedef struct BH_GPUVertexInputState
    {
        const BH_GPUVertexBufferDescription *vertex_buffer_descriptions;
        uint32_t num_vertex_buffers;

        const BH_GPUVertexAttribute *vertex_attributes;
        uint32_t num_vertex_attributes;
    } BH_GPUVertexInputState;

    typedef struct BH_GPURasterizerState
    {
        BH_GPUFillMode fill_mode;
        BH_GPUCullMode cull_mode;
        BH_GPUFrontFace front_face;
    } BH_GPURasterizerState;

    typedef struct BH_GPUMultisampleState
    {
        BH_GPUSampleCount sample_count;
    } BH_GPUMultisampleState;

    typedef struct BH_GPUDepthStencilState
    {
        bool enable_depth_test;
        bool enable_depth_write;
        BH_GPUCompareOp compare_op;
    } BH_GPUDepthStencilState;

    typedef struct BH_GPUColorTargetBlendState
    {
        bool enable_blend;

        BH_GPUBlendOp color_blend_op;
        BH_GPUBlendOp alpha_blend_op;

        BH_GPUBlendFactor src_color_blendfactor;
        BH_GPUBlendFactor dst_color_blendfactor;
        BH_GPUBlendFactor src_alpha_blendfactor;
        BH_GPUBlendFactor dst_alpha_blendfactor;

        bool enable_color_write_mask;
    } BH_GPUColorTargetBlendState;

    typedef struct BH_GPUColorTargetDescription
    {
        BH_GPUTextureFormat format;
        BH_GPUColorTargetBlendState blend_state;
    } BH_GPUColorTargetDescription;

    typedef struct BH_GPUGraphicsPipelineTargetInfo
    {
        const BH_GPUColorTargetDescription *color_target_descriptions;
        uint32_t num_color_targets;

        BH_GPUTextureFormat depth_stencil_format;
        bool has_depth_stencil_target;
    } BH_GPUGraphicsPipelineTargetInfo;

    typedef struct BH_GPUGraphicsPipelineCreateInfo
    {
        BH_GPUShader *vertex_shader;
        BH_GPUShader *fragment_shader;

        BH_GPUVertexInputState vertex_input_state;
        BH_GPUPrimitiveType primitive_type;

        BH_GPURasterizerState rasterizer_state;
        BH_GPUMultisampleState multisample_state;
        BH_GPUDepthStencilState depth_stencil_state;

        BH_GPUGraphicsPipelineTargetInfo target_info;
    } BH_GPUGraphicsPipelineCreateInfo;


    /* -------------------------------------------------------------------------
       Backend (virtual interface)
       ------------------------------------------------------------------------- */

    typedef struct BH_GPUBackendVTable
    {
        BH_GPUDevice *(*create_device)(BH_GPUShaderFormat shader_format, bool debug, const char *driver_name);
        void (*destroy_device)(BH_GPUDevice *device);

        bool (*claim_window)(BH_GPUDevice *device, BH_Window *window);
        void (*release_window)(BH_GPUDevice *device, BH_Window *window);

        void (*set_swapchain_parameters)(BH_GPUDevice *device, BH_Window *window, BH_GPUSwapchainComposition comp,
                                         BH_GPUPresentMode present_mode);
        BH_GPUTextureFormat (*get_swapchain_texture_format)(BH_GPUDevice *device, BH_Window *window);

        void (*wait_for_idle)(BH_GPUDevice *device);

        BH_GPUCommandBuffer *(*acquire_command_buffer)(BH_GPUDevice *device);
        void (*cancel_command_buffer)(BH_GPUCommandBuffer *cmd);
        bool (*submit_command_buffer)(BH_GPUCommandBuffer *cmd);

        void (*wait_and_acquire_swapchain_texture)(BH_GPUCommandBuffer *cmd, BH_Window *window,
                                                   BH_GPUTexture **out_texture, uint32_t *out_w, uint32_t *out_h);

        BH_GPUCopyPass *(*begin_copy_pass)(BH_GPUCommandBuffer *cmd);
        void (*end_copy_pass)(BH_GPUCopyPass *copy);

        BH_GPURenderPass *(*begin_render_pass)(BH_GPUCommandBuffer *cmd, const BH_GPUColorTargetInfo *color_targets,
                                               uint32_t num_color_targets,
                                               const BH_GPUDepthStencilTargetInfo *depth_target);
        void (*end_render_pass)(BH_GPURenderPass *pass);

        void (*set_viewport)(BH_GPURenderPass *pass, const BH_GPUViewport *viewport);
        void (*set_scissor)(BH_GPURenderPass *pass, const BH_GPU_Rect *rect);

        void (*bind_graphics_pipeline)(BH_GPURenderPass *pass, BH_GPUGraphicsPipeline *pipeline);
        void (*bind_vertex_buffers)(BH_GPURenderPass *pass, uint32_t first_slot, const BH_GPUBufferBinding *bindings,
                                    uint32_t num_bindings);
        void (*bind_index_buffer)(BH_GPURenderPass *pass, const BH_GPUBufferBinding *binding,
                                  BH_GPUIndexElementSize index_element_size);
        void (*bind_fragment_samplers)(BH_GPURenderPass *pass, uint32_t first_slot,
                                       const BH_GPUTextureSamplerBinding *bindings, uint32_t num_bindings);
        void (*draw_primitives)(BH_GPURenderPass *pass, uint32_t vertex_count, uint32_t instance_count,
                                uint32_t first_vertex, uint32_t first_instance);
        void (*draw_indexed_primitives)(BH_GPURenderPass *pass, uint32_t index_count, uint32_t instance_count,
                                        uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

        BH_GPUTexture *(*create_texture)(BH_GPUDevice *device, const BH_GPUTextureCreateInfo *info);
        void (*release_texture)(BH_GPUDevice *device, BH_GPUTexture *texture);

        BH_GPUBuffer *(*create_buffer)(BH_GPUDevice *device, const BH_GPUBufferCreateInfo *info);
        void (*release_buffer)(BH_GPUDevice *device, BH_GPUBuffer *buffer);

        BH_GPUTransferBuffer *(*create_transfer_buffer)(BH_GPUDevice *device, const BH_GPUTransferBufferCreateInfo *info);
        void (*release_transfer_buffer)(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer);

        void *(*map_transfer_buffer)(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer, bool cycle);
        void (*unmap_transfer_buffer)(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer);

        void (*upload_to_texture)(BH_GPUCopyPass *copy, const BH_GPUTextureTransferInfo *src,
                                  const BH_GPUTextureRegion *dst, bool cycle);
        void (*upload_to_buffer)(BH_GPUCopyPass *copy, const BH_GPUTransferBufferLocation *src,
                                 const BH_GPUBufferRegion *dst, bool cycle);

        BH_GPUSampler *(*create_sampler)(BH_GPUDevice *device, const BH_GPUSamplerCreateInfo *info);
        void (*release_sampler)(BH_GPUDevice *device, BH_GPUSampler *sampler);

        BH_GPUShader *(*create_shader)(BH_GPUDevice *device, const BH_GPUShaderCreateInfo *info);
        void (*release_shader)(BH_GPUDevice *device, BH_GPUShader *shader);

        BH_GPUGraphicsPipeline *(*create_graphics_pipeline)(BH_GPUDevice *device,
                                                            const BH_GPUGraphicsPipelineCreateInfo *info);
        void (*release_graphics_pipeline)(BH_GPUDevice *device, BH_GPUGraphicsPipeline *pipeline);

        void (*push_vertex_uniform_data)(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size);
        void (*push_fragment_uniform_data)(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size);


        /* Error string for the current thread (may be backend-global). */
        const char *(*get_last_error)(void);

        /* Backend-native format helpers (tokens). */
        BH_GPUTextureFormat (*get_texture_format_r8g8b8a8_unorm)(void);
        BH_GPUTextureFormat (*get_texture_format_r8g8b8a8_unorm_srgb)(void);
        BH_GPUTextureFormat (*get_texture_format_d32_float)(void);
    } BH_GPUBackendVTable;

    typedef struct BH_GPUBackend
    {
        const char *name;
        const BH_GPUBackendVTable *vt;
    } BH_GPUBackend;

    /* Known backends */
    extern const BH_GPUBackend BH_GPU_BACKEND_SDL;
    extern const BH_GPUBackend BH_GPU_BACKEND_GL;

    /* -------------------------------------------------------------------------
       Backend selection
       ------------------------------------------------------------------------- */

    const BH_GPUBackend *BH_GPU_GetBackend(void);
    void BH_GPU_SetBackend(const BH_GPUBackend *backend);

    /* -------------------------------------------------------------------------
       Convenience wrappers (call through vtable)
       ------------------------------------------------------------------------- */

    BH_GPUDevice *BH_GPU_CreateDevice(BH_GPUShaderFormat shader_format, bool debug, const char *driver_name);
    void BH_GPU_DestroyDevice(BH_GPUDevice *device);

    bool BH_GPU_ClaimWindowForDevice(BH_GPUDevice *device, BH_Window *window);
    void BH_GPU_ReleaseWindowFromDevice(BH_GPUDevice *device, BH_Window *window);

    void BH_GPU_SetSwapchainParameters(BH_GPUDevice *device, BH_Window *window, BH_GPUSwapchainComposition comp,
                                       BH_GPUPresentMode present_mode);
    BH_GPUTextureFormat BH_GPU_GetSwapchainTextureFormat(BH_GPUDevice *device, BH_Window *window);

    void BH_GPU_WaitForIdle(BH_GPUDevice *device);

    BH_GPUCommandBuffer *BH_GPU_AcquireCommandBuffer(BH_GPUDevice *device);
    void BH_GPU_CancelCommandBuffer(BH_GPUCommandBuffer *cmd);
    bool BH_GPU_SubmitCommandBuffer(BH_GPUCommandBuffer *cmd);

    void BH_GPU_WaitAndAcquireSwapchainTexture(BH_GPUCommandBuffer *cmd, BH_Window *window, BH_GPUTexture **out_texture,
                                               uint32_t *out_w, uint32_t *out_h);

    BH_GPUCopyPass *BH_GPU_BeginCopyPass(BH_GPUCommandBuffer *cmd);
    void BH_GPU_EndCopyPass(BH_GPUCopyPass *copy);

    BH_GPURenderPass *BH_GPU_BeginRenderPass(BH_GPUCommandBuffer *cmd, const BH_GPUColorTargetInfo *color_targets,
                                             uint32_t num_color_targets,
                                             const BH_GPUDepthStencilTargetInfo *depth_target);
    void BH_GPU_EndRenderPass(BH_GPURenderPass *pass);

    void BH_GPU_SetViewport(BH_GPURenderPass *pass, const BH_GPUViewport *viewport);
    void BH_GPU_SetScissor(BH_GPURenderPass *pass, const BH_GPU_Rect *rect);

    void BH_GPU_BindGraphicsPipeline(BH_GPURenderPass *pass, BH_GPUGraphicsPipeline *pipeline);
    void BH_GPU_BindVertexBuffers(BH_GPURenderPass *pass, uint32_t first_slot, const BH_GPUBufferBinding *bindings,
                                  uint32_t num_bindings);
    void BH_GPU_BindIndexBuffer(BH_GPURenderPass *pass, const BH_GPUBufferBinding *binding,
                                BH_GPUIndexElementSize index_element_size);
    void BH_GPU_BindFragmentSamplers(BH_GPURenderPass *pass, uint32_t first_slot,
                                     const BH_GPUTextureSamplerBinding *bindings, uint32_t num_bindings);
    void BH_GPU_DrawPrimitives(BH_GPURenderPass *pass, uint32_t vertex_count, uint32_t instance_count,
                               uint32_t first_vertex, uint32_t first_instance);
    void BH_GPU_DrawIndexedPrimitives(BH_GPURenderPass *pass, uint32_t index_count, uint32_t instance_count,
                                      uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

    BH_GPUTexture *BH_GPU_CreateTexture(BH_GPUDevice *device, const BH_GPUTextureCreateInfo *info);
    void BH_GPU_ReleaseTexture(BH_GPUDevice *device, BH_GPUTexture *texture);

    BH_GPUBuffer *BH_GPU_CreateBuffer(BH_GPUDevice *device, const BH_GPUBufferCreateInfo *info);
    void BH_GPU_ReleaseBuffer(BH_GPUDevice *device, BH_GPUBuffer *buffer);

    BH_GPUTransferBuffer *BH_GPU_CreateTransferBuffer(BH_GPUDevice *device, const BH_GPUTransferBufferCreateInfo *info);
    void BH_GPU_ReleaseTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer);

    void *BH_GPU_MapTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer, bool cycle);
    void BH_GPU_UnmapTransferBuffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer);

    void BH_GPU_UploadToTexture(BH_GPUCopyPass *copy, const BH_GPUTextureTransferInfo *src,
                                const BH_GPUTextureRegion *dst, bool cycle);
    void BH_GPU_UploadToBuffer(BH_GPUCopyPass *copy, const BH_GPUTransferBufferLocation *src, const BH_GPUBufferRegion *dst,
                               bool cycle);

    BH_GPUSampler *BH_GPU_CreateSampler(BH_GPUDevice *device, const BH_GPUSamplerCreateInfo *info);
    void BH_GPU_ReleaseSampler(BH_GPUDevice *device, BH_GPUSampler *sampler);

    BH_GPUShader *BH_GPU_CreateShader(BH_GPUDevice *device, const BH_GPUShaderCreateInfo *info);
    void BH_GPU_ReleaseShader(BH_GPUDevice *device, BH_GPUShader *shader);

    BH_GPUGraphicsPipeline *BH_GPU_CreateGraphicsPipeline(BH_GPUDevice *device, const BH_GPUGraphicsPipelineCreateInfo *info);
    void BH_GPU_ReleaseGraphicsPipeline(BH_GPUDevice *device, BH_GPUGraphicsPipeline *pipeline);

    void BH_GPU_PushVertexUniformData(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size);
    void BH_GPU_PushFragmentUniformData(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size);

    const char *BH_GPU_GetLastError(void);

    /* Backend-native format token helpers. */
    BH_GPUTextureFormat BH_GPU_GetTextureFormat_R8G8B8A8_UNORM(void);
    BH_GPUTextureFormat BH_GPU_GetTextureFormat_R8G8B8A8_UNORM_SRGB(void);
    BH_GPUTextureFormat BH_GPU_GetTextureFormat_D32_FLOAT(void);

#ifdef __cplusplus
}
#endif
