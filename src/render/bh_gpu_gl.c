// -----------------------------------------------------------------------------
// bh_gpu_gl.c
// -----------------------------------------------------------------------------

#include "bh_gpu.h"
#include "bh_shader_reflection.h"

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <vendor/glad/gl.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// Desktop OpenGL implementation
// -----------------------------------------------------------------------------

static char g_gl_last_error[512] = {0};

static void bh_gl_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(g_gl_last_error, sizeof(g_gl_last_error), fmt, ap);
    va_end(ap);
}

// -----------------------------------------------------------------------------
// Backend object model
// -----------------------------------------------------------------------------

typedef struct BH_GLTexture
{
    GLuint id;
    GLenum target;
    GLenum internal_format;
    GLenum upload_format;
    GLenum upload_type;
    uint32_t width;
    uint32_t height;
    bool is_depth;
    bool is_swapchain;
} BH_GLTexture;

typedef struct BH_GLBuffer
{
    GLuint id;
    GLenum target;
    uint32_t size;
} BH_GLBuffer;

typedef struct BH_GLTransferBuffer
{
    void *data;
    uint32_t size;
} BH_GLTransferBuffer;

typedef struct BH_GLSampler
{
    GLuint id;
} BH_GLSampler;

typedef struct BH_GLShader
{
    GLuint id;
    GLenum stage;
    BH_GPUShaderFormat format;
} BH_GLShader;

typedef struct BH_GLPipeline
{
    GLuint program;
    GLuint vao;

    BH_GPUPrimitiveType primitive;
    BH_GPUDepthStencilState depth;
    BH_GPURasterizerState rast;
    BH_GPUColorTargetBlendState blend;

    BH_GPUVertexInputState vis;

    // Owned copies of vertex input state arrays.
    //
    // IMPORTANT: Many call sites build vertex input descriptions/attributes
    // on the stack when creating pipelines. The OpenGL backend uses these
    // arrays during draw-time VAO setup, so it must deep-copy them so the
    // pointers remain valid for the lifetime of the pipeline.
    BH_GPUVertexBufferDescription *owned_vb_descs;
    uint32_t owned_num_vb_descs;
    BH_GPUVertexAttribute *owned_vertex_attrs;
    uint32_t owned_num_vertex_attrs;
} BH_GLPipeline;

typedef struct BH_GLUniformBuffer
{
    GLuint id;
    uint32_t capacity;
} BH_GLUniformBuffer;

typedef struct BH_GLCommandBuffer BH_GLCommandBuffer;
typedef struct BH_GLCopyPass BH_GLCopyPass;
typedef struct BH_GLRenderPass BH_GLRenderPass;

typedef struct BH_GLDevice
{
    bool debug;
    BH_GPUShaderFormat shader_format;

    SDL_Window *window;
    SDL_GLContext glctx;

    // pseudo-swapchain
    BH_GLTexture swap_tex;
    GLuint swap_fbo;

    BH_GLUniformBuffer vs_ubos[BH_GPU_MAX_UNIFORM_SLOTS];
    BH_GLUniformBuffer fs_ubos[BH_GPU_MAX_UNIFORM_SLOTS];

    BH_GLCommandBuffer *free_cmds;
    BH_GLCopyPass *free_copy_passes;
    BH_GLRenderPass *free_render_passes;

    bool scissor_enabled;
} BH_GLDevice;

typedef struct BH_GLCommandBuffer
{
    BH_GLDevice *dev;
    struct BH_GLCommandBuffer *next;
    bool wants_present;
    uint32_t fb_w;
    uint32_t fb_h;
} BH_GLCommandBuffer;

typedef struct BH_GLCopyPass
{
    BH_GLCommandBuffer *cmd;
    struct BH_GLCopyPass *next;
} BH_GLCopyPass;

typedef struct BH_GLRenderPass
{
    BH_GLCommandBuffer *cmd;
    BH_GLDevice *dev;
    struct BH_GLRenderPass *next;

    GLuint fbo;
    bool owns_fbo;

    uint32_t fb_w;
    uint32_t fb_h;

    BH_GLPipeline *pipeline;
    GLenum gl_prim;

    // per-slot VB binding state (for attrib setup)
    BH_GLBuffer *vertex_buffers[8];
    uint32_t vertex_offsets[8];

    BH_GLBuffer *index_buffer;
    uint32_t index_element_size;
    GLenum index_type;
} BH_GLRenderPass;

// Keep VS and FS uniform slots separate in OpenGL binding space.
enum
{
    BH_GL_VS_UBO_BIND_BASE = 0,
    BH_GL_FS_UBO_BIND_BASE = 8,
};

static GLenum bh_gl_prim_mode(BH_GPUPrimitiveType t)
{
    switch (t)
    {
    case BH_GPU_PRIMITIVETYPE_LINELIST:
        return GL_LINES;
    case BH_GPU_PRIMITIVETYPE_TRIANGLELIST:
    default:
        return GL_TRIANGLES;
    }
}

static GLenum bh_gl_compare(BH_GPUCompareOp op)
{
    switch (op)
    {
    case BH_GPU_COMPAREOP_NEVER:
        return GL_NEVER;
    case BH_GPU_COMPAREOP_LESS:
        return GL_LESS;
    case BH_GPU_COMPAREOP_EQUAL:
        return GL_EQUAL;
    case BH_GPU_COMPAREOP_LESS_OR_EQUAL:
        return GL_LEQUAL;
    case BH_GPU_COMPAREOP_GREATER:
        return GL_GREATER;
    case BH_GPU_COMPAREOP_NOT_EQUAL:
        return GL_NOTEQUAL;
    case BH_GPU_COMPAREOP_GREATER_OR_EQUAL:
        return GL_GEQUAL;
    case BH_GPU_COMPAREOP_ALWAYS:
    default:
        return GL_ALWAYS;
    }
}

static GLenum bh_gl_blend_factor(BH_GPUBlendFactor f)
{
    switch (f)
    {
    case BH_GPU_BLENDFACTOR_ZERO:
        return GL_ZERO;
    case BH_GPU_BLENDFACTOR_ONE:
        return GL_ONE;
    case BH_GPU_BLENDFACTOR_SRC_ALPHA:
        return GL_SRC_ALPHA;
    case BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    default:
        return GL_ONE;
    }
}

static GLenum bh_gl_blend_op(BH_GPUBlendOp op)
{
    switch (op)
    {
    case BH_GPU_BLENDOP_ADD:
    default:
        return GL_FUNC_ADD;
    }
}

static bool bh_gl_vertex_format(BH_GPUVertexElementFormat fmt, GLint *out_size, GLenum *out_type, GLboolean *out_norm)
{
    *out_type = GL_FLOAT;
    *out_norm = GL_FALSE;

    switch (fmt)
    {
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT2:
        *out_size = 2;
        return true;
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT3:
        *out_size = 3;
        return true;
    case BH_GPU_VERTEXELEMENTFORMAT_FLOAT4:
        *out_size = 4;
        return true;
    case BH_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM:
        *out_size = 4;
        *out_type = GL_UNSIGNED_BYTE;
        *out_norm = GL_TRUE;
        return true;
    default:
        return false;
    }
}

static void bh_gl_apply_vertex_bindings(BH_GLRenderPass *p)
{
    if (!p || !p->pipeline)
        return;

    glBindVertexArray(p->pipeline->vao);

    const BH_GPUVertexInputState *vis = &p->pipeline->vis;

    for (uint32_t a = 0; a < vis->num_vertex_attributes; ++a)
    {
        const BH_GPUVertexAttribute *attr = &vis->vertex_attributes[a];

        // Skip invalid/optimized-out attributes defensively (GL uses -1 for invalid attribs)
        if ((int)attr->location < 0)
            continue;

        const uint32_t bslot = attr->buffer_slot;

        // Find pitch for this buffer slot.
        uint32_t pitch = 0;
        for (uint32_t b = 0; b < vis->num_vertex_buffers; ++b)
        {
            if (vis->vertex_buffer_descriptions[b].slot == bslot)
            {
                pitch = vis->vertex_buffer_descriptions[b].pitch;
                break;
            }
        }

        BH_GLBuffer *buf = NULL;
        uint32_t base_off = 0;
        if (bslot < (uint32_t)(sizeof(p->vertex_buffers) / sizeof(p->vertex_buffers[0])))
        {
            buf = p->vertex_buffers[bslot];
            base_off = p->vertex_offsets[bslot];
        }

        // If no buffer bound for this attribute, skip; keep existing VAO state.
        if (!buf)
            continue;

        GLint size = 0;
        GLenum type = GL_FLOAT;
        GLboolean norm = GL_FALSE;
        if (!bh_gl_vertex_format(attr->format, &size, &type, &norm))
        {
            glDisableVertexAttribArray(attr->location);
            continue;
        }

        glBindBuffer(GL_ARRAY_BUFFER, buf->id);
        glEnableVertexAttribArray(attr->location);
        glVertexAttribPointer(attr->location, size, type, norm, (GLsizei)pitch,
                              (const void *)(uintptr_t)(base_off + attr->offset));

        // Instance rate (if available).
        uint32_t rate = 0;
        for (uint32_t b = 0; b < vis->num_vertex_buffers; ++b)
        {
            if (vis->vertex_buffer_descriptions[b].slot == bslot)
            {
                rate = (vis->vertex_buffer_descriptions[b].input_rate == BH_GPU_VERTEXINPUTRATE_INSTANCE) ? 1u : 0u;
                break;
            }
        }
#if defined(GL_ES_VERSION_3_0) || defined(GL_VERSION_3_3) || defined(GL_ARB_instanced_arrays)
        glVertexAttribDivisor(attr->location, rate);
#endif
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void bh_gl_apply_index_binding(BH_GLRenderPass *p)
{
    if (!p || !p->pipeline || !p->index_buffer)
        return;

    glBindVertexArray(p->pipeline->vao);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->index_buffer->id);
}

static void bh_gl_apply_pipeline_state(BH_GLDevice *dev, const BH_GLPipeline *p)
{
    (void)dev;

    // Depth
    if (p->depth.enable_depth_test)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(bh_gl_compare(p->depth.compare_op));
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(p->depth.enable_depth_write ? GL_TRUE : GL_FALSE);

    // Raster
    if (p->rast.cull_mode == BH_GPU_CULLMODE_NONE)
    {
        glDisable(GL_CULL_FACE);
    }
    else
    {
        glEnable(GL_CULL_FACE);
        glCullFace(p->rast.cull_mode == BH_GPU_CULLMODE_FRONT ? GL_FRONT : GL_BACK);
    }
    glFrontFace(p->rast.front_face == BH_GPU_FRONTFACE_CLOCKWISE ? GL_CW : GL_CCW);

    // Blend
    if (p->blend.enable_blend)
    {
        glEnable(GL_BLEND);
        // Many pipelines use separate factors for color/alpha; preserve that intent.
        glBlendEquationSeparate(bh_gl_blend_op(p->blend.color_blend_op), bh_gl_blend_op(p->blend.alpha_blend_op));
        glBlendFuncSeparate(
            bh_gl_blend_factor(p->blend.src_color_blendfactor), bh_gl_blend_factor(p->blend.dst_color_blendfactor),
            bh_gl_blend_factor(p->blend.src_alpha_blendfactor), bh_gl_blend_factor(p->blend.dst_alpha_blendfactor));
    }
    else
    {
        glDisable(GL_BLEND);
    }

    /*
        Match SDL_gpu's semantics:
        - If enable_color_write_mask is FALSE, writes are enabled for all channels.
        - If TRUE, a per-channel mask would normally be applied. This backend's
          abstraction currently lacks the mask bits, so we treat it as "mask=0".
    */
    if (!p->blend.enable_color_write_mask)
    {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    else
    {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
}

static void bh_gl_destroy_swapchain(BH_GLDevice *dev)
{
    if (dev->swap_fbo)
    {
        glDeleteFramebuffers(1, &dev->swap_fbo);
        dev->swap_fbo = 0;
    }

    if (dev->swap_tex.id)
    {
        glDeleteTextures(1, &dev->swap_tex.id);
        dev->swap_tex.id = 0;
    }

    dev->swap_tex.width = 0;
    dev->swap_tex.height = 0;
    dev->swap_tex.is_swapchain = false;
}

static bool bh_gl_ensure_swapchain(BH_GLDevice *dev, uint32_t w, uint32_t h)
{
    if (!dev || !dev->window)
    {
        bh_gl_set_error("OpenGL: device not initialized");
        return false;
    }

    if (dev->swap_tex.id && dev->swap_tex.width == w && dev->swap_tex.height == h)
    {
        return true;
    }

    // Recreate swap resources.
    if (dev->swap_tex.id)
    {
        glDeleteTextures(1, &dev->swap_tex.id);
        dev->swap_tex.id = 0;
    }

    if (!dev->swap_fbo)
    {
        glGenFramebuffers(1, &dev->swap_fbo);
    }

    glGenTextures(1, &dev->swap_tex.id);
    dev->swap_tex.target = GL_TEXTURE_2D;
    dev->swap_tex.internal_format = GL_RGBA8;
    dev->swap_tex.upload_format = GL_RGBA;
    dev->swap_tex.upload_type = GL_UNSIGNED_BYTE;
    dev->swap_tex.width = w;
    dev->swap_tex.height = h;
    dev->swap_tex.is_depth = false;
    dev->swap_tex.is_swapchain = true;

    glBindTexture(GL_TEXTURE_2D, dev->swap_tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)dev->swap_tex.internal_format, (GLsizei)w, (GLsizei)h, 0,
                 dev->swap_tex.upload_format, dev->swap_tex.upload_type, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, dev->swap_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dev->swap_tex.id, 0);

    GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, bufs);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        bh_gl_set_error("OpenGL: swap FBO incomplete (0x%x)", (unsigned)status);
        return false;
    }

    return true;
}

static void bh_gl_init_uniform_buffers(BH_GLDevice *dev)
{
    for (uint32_t i = 0; i < BH_GPU_MAX_UNIFORM_SLOTS; ++i)
    {
        if (!dev->vs_ubos[i].id)
        {
            glGenBuffers(1, &dev->vs_ubos[i].id);
            dev->vs_ubos[i].capacity = 0;
        }
        if (!dev->fs_ubos[i].id)
        {
            glGenBuffers(1, &dev->fs_ubos[i].id);
            dev->fs_ubos[i].capacity = 0;
        }
    }
}

static void bh_gl_destroy_uniform_buffers(BH_GLDevice *dev)
{
    GLuint ids[1];
    for (uint32_t i = 0; i < BH_GPU_MAX_UNIFORM_SLOTS; ++i)
    {
        if (dev->vs_ubos[i].id)
        {
            ids[0] = dev->vs_ubos[i].id;
            glDeleteBuffers(1, ids);
            dev->vs_ubos[i].id = 0;
            dev->vs_ubos[i].capacity = 0;
        }
        if (dev->fs_ubos[i].id)
        {
            ids[0] = dev->fs_ubos[i].id;
            glDeleteBuffers(1, ids);
            dev->fs_ubos[i].id = 0;
            dev->fs_ubos[i].capacity = 0;
        }
    }
}

// -----------------------------------------------------------------------------
// Vtable impl
// -----------------------------------------------------------------------------

static BH_GPUDevice *bh_gl_create_device(BH_GPUShaderFormat shader_format, bool debug, const char *driver_name)
{
    (void)driver_name;

    BH_GLDevice *dev = (BH_GLDevice *)SDL_calloc(1, sizeof(BH_GLDevice));
    if (!dev)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    dev->debug = debug;
    dev->shader_format = shader_format;

    SDL_Log("Device created");

    return (BH_GPUDevice *)dev;
}

static void bh_gl_destroy_device(BH_GPUDevice *device)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev)
        return;

    while (dev->free_render_passes)
    {
        BH_GLRenderPass *n = dev->free_render_passes->next;
        SDL_free(dev->free_render_passes);
        dev->free_render_passes = n;
    }

    while (dev->free_copy_passes)
    {
        BH_GLCopyPass *n = dev->free_copy_passes->next;
        SDL_free(dev->free_copy_passes);
        dev->free_copy_passes = n;
    }

    while (dev->free_cmds)
    {
        BH_GLCommandBuffer *n = dev->free_cmds->next;
        SDL_free(dev->free_cmds);
        dev->free_cmds = n;
    }

    if (dev->window && dev->glctx)
    {
        (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
        bh_gl_destroy_uniform_buffers(dev);
        bh_gl_destroy_swapchain(dev);
        SDL_GL_DestroyContext(dev->glctx);
        dev->glctx = NULL;
    }

    dev->window = NULL;
    SDL_free(dev);
}

static bool bh_gl_claim_window(BH_GPUDevice *device, BH_Window *window)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    SDL_Window *w = (SDL_Window *)window;

    if (!dev || !w)
    {
        bh_gl_set_error("OpenGL: claim_window invalid args");
        return false;
    }

    dev->window = w;

    dev->glctx = SDL_GL_CreateContext(w);
    if (!dev->glctx)
    {
        bh_gl_set_error("OpenGL: SDL_GL_CreateContext failed: %s", SDL_GetError());
        dev->window = NULL;
        return false;
    }

    if (!SDL_GL_MakeCurrent(w, dev->glctx))
    {
        bh_gl_set_error("OpenGL: SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        SDL_GL_DestroyContext(dev->glctx);
        dev->glctx = NULL;
        dev->window = NULL;
        return false;
    }

    #ifndef __EMSCRIPTEN__
    // Only load function pointers on Desktop
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        bh_gl_set_error("Failed to initialize GLAD");
        return false;
    }
    #endif

    bh_gl_init_uniform_buffers(dev);

    // Conservative defaults.
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    dev->scissor_enabled = false;

    return true;
}

static void bh_gl_release_window(BH_GPUDevice *device, BH_Window *window)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    SDL_Window *w = (SDL_Window *)window;

    if (!dev || !w || dev->window != w)
        return;

    if (dev->glctx)
    {
        (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
        bh_gl_destroy_uniform_buffers(dev);
        bh_gl_destroy_swapchain(dev);
        SDL_GL_DestroyContext(dev->glctx);
        dev->glctx = NULL;
    }

    dev->window = NULL;
}

static void bh_gl_set_swapchain_parameters(BH_GPUDevice *device, BH_Window *window, BH_GPUSwapchainComposition comp,
                                           BH_GPUPresentMode present_mode)
{
    (void)comp;

    BH_GLDevice *dev = (BH_GLDevice *)device;
    SDL_Window *w = (SDL_Window *)window;
    if (!dev || !w)
        return;

    // Ensure context is current before setting interval.
    if (dev->glctx)
        (void)SDL_GL_MakeCurrent(w, dev->glctx);

    int interval = 0;
    switch (present_mode)
    {
    case BH_GPU_PRESENTMODE_VSYNC:
        interval = 1;
        break;
    case BH_GPU_PRESENTMODE_IMMEDIATE:
    default:
        interval = 0;
        break;
    }

    (void)SDL_GL_SetSwapInterval(interval);
}

static BH_GPUTextureFormat bh_gl_get_swapchain_texture_format(BH_GPUDevice *device, BH_Window *window)
{
    (void)device;
    (void)window;
    return (BH_GPUTextureFormat)GL_RGBA8;
}

static void bh_gl_wait_for_idle(BH_GPUDevice *device)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !dev->window || !dev->glctx)
        return;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    glFinish();
}

static void bh_gl_recycle_command_buffer(BH_GLCommandBuffer *c)
{
    BH_GLDevice *dev = c->dev;
    c->wants_present = false;
    c->fb_w = 0;
    c->fb_h = 0;
    c->next = dev->free_cmds;
    dev->free_cmds = c;
}

static BH_GPUCommandBuffer *bh_gl_acquire_command_buffer(BH_GPUDevice *device)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !dev->window || !dev->glctx)
    {
        bh_gl_set_error("OpenGL: acquire_command_buffer called before claim_window");
        return NULL;
    }

    BH_GLCommandBuffer *cmd = dev->free_cmds;
    if (cmd)
    {
        dev->free_cmds = cmd->next;
        cmd->next = NULL;
        cmd->wants_present = false;
        cmd->fb_w = 0;
        cmd->fb_h = 0;
        cmd->dev = dev;
        return (BH_GPUCommandBuffer *)cmd;
    }

    cmd = (BH_GLCommandBuffer *)SDL_calloc(1, sizeof(BH_GLCommandBuffer));
    if (!cmd)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    cmd->dev = dev;
    return (BH_GPUCommandBuffer *)cmd;
}

static void bh_gl_cancel_command_buffer(BH_GPUCommandBuffer *cmd)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev)
    {
        if (c)
            SDL_free(c);
        return;
    }
    bh_gl_recycle_command_buffer(c);
}

static bool bh_gl_submit_command_buffer(BH_GPUCommandBuffer *cmd)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev)
    {
        if (c)
            SDL_free(c);
        return false;
    }

    BH_GLDevice *dev = c->dev;

    // 1. Check if Context Switch Fails
    if (!SDL_GL_MakeCurrent(dev->window, dev->glctx))
    {
        bh_gl_set_error("Submit: SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        bh_gl_recycle_command_buffer(c);
        return false;
    }

    if (c->wants_present)
    {
        // 2. Validate Read Framebuffer (Your internal swap texture)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, dev->swap_fbo);
        GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            bh_gl_set_error("Submit: Read FBO (Swapchain) incomplete: 0x%04x", status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0); // Restore
            bh_gl_recycle_command_buffer(c);
            return false;
        }

        // 3. Bind Default FBO for Drawing
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Paranoid check: ensure the default framebuffer accepts drawing
        // (Rarely fails, but good for debugging specific driver issues)
        // status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

        // Ensure no scissor affects the blit.
        glDisable(GL_SCISSOR_TEST);
        dev->scissor_enabled = false;

        // 4. Clear existing GL errors so we catch exactly the Blit error
        while (glGetError() != GL_NO_ERROR)
            ;

        glBlitFramebuffer(0, 0, (GLint)c->fb_w, (GLint)c->fb_h, 0, 0, (GLint)c->fb_w, (GLint)c->fb_h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // 5. Check for Blit Failure (Likely culprit)
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            bh_gl_set_error("Submit: glBlitFramebuffer failed: 0x%04x (Dim: %dx%d)", err, c->fb_w, c->fb_h);
            bh_gl_recycle_command_buffer(c);
            return false;
        }

        // 6. Check Swap Window
        if (!SDL_GL_SwapWindow(dev->window))
        {
            bh_gl_set_error("Submit: SDL_GL_SwapWindow failed: %s", SDL_GetError());
            bh_gl_recycle_command_buffer(c);
            return false;
        }
    }
    bh_gl_recycle_command_buffer(c);
    return true;
}

static void bh_gl_wait_and_acquire_swapchain_texture(BH_GPUCommandBuffer *cmd, BH_Window *window,
                                                     BH_GPUTexture **out_texture, uint32_t *out_w, uint32_t *out_h)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    SDL_Window *w = (SDL_Window *)window;

    if (out_texture)
        *out_texture = NULL;
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;

    if (!c || !c->dev || !w || !out_texture || !out_w || !out_h)
    {
        bh_gl_set_error("OpenGL: wait_and_acquire_swapchain_texture invalid args");
        return;
    }

    BH_GLDevice *dev = c->dev;
    if (!SDL_GL_MakeCurrent(w, dev->glctx))
    {
        bh_gl_set_error("OpenGL: SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return;
    }

    int ww = 0, hh = 0;
    SDL_GetWindowSizeInPixels(w, &ww, &hh);
    if (ww <= 0 || hh <= 0)
    {
        bh_gl_set_error("OpenGL: invalid window size");
        return;
    }

    if (!bh_gl_ensure_swapchain(dev, (uint32_t)ww, (uint32_t)hh))
        return;

    c->wants_present = true;
    c->fb_w = (uint32_t)ww;
    c->fb_h = (uint32_t)hh;

    *out_texture = (BH_GPUTexture *)&dev->swap_tex;
    *out_w = (uint32_t)ww;
    *out_h = (uint32_t)hh;
}

static BH_GPUCopyPass *bh_gl_begin_copy_pass(BH_GPUCommandBuffer *cmd)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev)
        return NULL;

    BH_GLDevice *dev = c->dev;

    BH_GLCopyPass *pass = dev->free_copy_passes;
    if (pass)
    {
        dev->free_copy_passes = pass->next;
        pass->next = NULL;
        pass->cmd = c;
        return (BH_GPUCopyPass *)pass;
    }

    pass = (BH_GLCopyPass *)SDL_calloc(1, sizeof(BH_GLCopyPass));
    if (!pass)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    pass->cmd = c;
    return (BH_GPUCopyPass *)pass;
}

static void bh_gl_end_copy_pass(BH_GPUCopyPass *pass)
{
    BH_GLCopyPass *p = (BH_GLCopyPass *)pass;
    if (!p || !p->cmd || !p->cmd->dev)
    {
        if (p)
            SDL_free(p);
        return;
    }

    BH_GLDevice *dev = p->cmd->dev;
    p->cmd = NULL;
    p->next = dev->free_copy_passes;
    dev->free_copy_passes = p;
}

static BH_GPURenderPass *bh_gl_begin_render_pass(BH_GPUCommandBuffer *cmd, const BH_GPUColorTargetInfo *color_targets,
                                                 uint32_t num_color_targets,
                                                 const BH_GPUDepthStencilTargetInfo *depth_target)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev || !color_targets || num_color_targets == 0)
    {
        bh_gl_set_error("OpenGL: begin_render_pass invalid args");
        return NULL;
    }

    BH_GLDevice *dev = c->dev;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLTexture *color0 = (BH_GLTexture *)color_targets[0].texture;
    if (!color0)
    {
        bh_gl_set_error("OpenGL: render pass missing color target");
        return NULL;
    }

    BH_GLRenderPass *pass = dev->free_render_passes;
    if (pass)
    {
        dev->free_render_passes = pass->next;
        memset(pass, 0, sizeof(*pass));
    }
    else
    {
        pass = (BH_GLRenderPass *)SDL_calloc(1, sizeof(BH_GLRenderPass));
        if (!pass)
        {
            bh_gl_set_error("OpenGL: out of memory");
            return NULL;
        }
    }

    pass->cmd = c;
    pass->dev = dev;
    pass->pipeline = NULL;
    pass->gl_prim = GL_TRIANGLES;

    pass->fb_w = color0->width;
    pass->fb_h = color0->height;

    // FBO selection
    if (color0->is_swapchain)
    {
        pass->fbo = dev->swap_fbo;
        pass->owns_fbo = false;

        glBindFramebuffer(GL_FRAMEBUFFER, dev->swap_fbo);
        // Color already attached by ensure_swapchain.
    }
    else
    {
        glGenFramebuffers(1, &pass->fbo);
        pass->owns_fbo = true;

        glBindFramebuffer(GL_FRAMEBUFFER, pass->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color0->target, color0->id, 0);
        GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);
    }

    // Attach depth if requested and supported.
    // IMPORTANT: dev->swap_fbo is persistent across frames; detach the depth
    // attachment when not in use to avoid size-mismatch FBO incompleteness.
    if (depth_target && depth_target->texture)
    {
        BH_GLTexture *dtex = (BH_GLTexture *)depth_target->texture;
        if (dtex && dtex->is_depth)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, dtex->target, dtex->id, 0);
        }
        else
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
    }
    else
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        bh_gl_set_error("OpenGL: render pass FBO incomplete (0x%x)", (unsigned)status);
        if (pass->owns_fbo)
        {
            glDeleteFramebuffers(1, &pass->fbo);
        }
        pass->dev = dev;
        memset(pass, 0, sizeof(*pass));
        pass->dev = dev;
        pass->next = dev->free_render_passes;
        dev->free_render_passes = pass;
        return NULL;
    }

    // ---------------------------------------------------------------------
    // Clears in OpenGL are affected by several pieces of global state:
    //  - GL_SCISSOR_TEST (clear only affects the scissor rect)
    //  - glColorMask (color channels can be masked out)
    //  - glDepthMask (depth clear is masked out when FALSE)
    //
    // Other backends (SDL_gpu/D3D/Vulkan) perform render-pass clears
    // independent of these state bits, but OpenGL does not. Because our
    // pipeline state can legitimately leave depth writes disabled (e.g.
    // transparent/debug draws), we must force masks to a known-good state
    // before issuing the clear to avoid stale depth/color contents causing
    // flickering/vanishing geometry.
    // ---------------------------------------------------------------------

    glDisable(GL_SCISSOR_TEST);
    dev->scissor_enabled = false;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    // Apply clears.
    GLbitfield clear_mask = 0;
    if (color_targets[0].load_op == BH_GPU_LOADOP_CLEAR)
    {
        glClearColor(color_targets[0].clear_color.r, color_targets[0].clear_color.g, color_targets[0].clear_color.b,
                     color_targets[0].clear_color.a);
        clear_mask |= GL_COLOR_BUFFER_BIT;
    }
    if (depth_target && depth_target->texture && depth_target->load_op == BH_GPU_LOADOP_CLEAR)
    {
#ifdef __EMSCRIPTEN__
        // WebGL uses the float version
        glClearDepthf(depth_target->clear_depth);
#else
        // Desktop OpenGL uses the double version
        glClearDepth((GLdouble)depth_target->clear_depth);
#endif
        clear_mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (clear_mask)
        glClear(clear_mask);

    // Default viewport/scissor to full target; higher level code usually overrides.
    glViewport(0, 0, (GLsizei)pass->fb_w, (GLsizei)pass->fb_h);
    // Scissor was disabled above for correct clears.

    return (BH_GPURenderPass *)pass;
}

static void bh_gl_end_render_pass(BH_GPURenderPass *pass)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !p->dev)
    {
        if (p)
            SDL_free(p);
        return;
    }

    if (p->owns_fbo && p->fbo)
    {
        glDeleteFramebuffers(1, &p->fbo);
    }

    BH_GLDevice *dev = p->dev;
    memset(p, 0, sizeof(*p));
    p->dev = dev;
    p->next = dev->free_render_passes;
    dev->free_render_passes = p;
}

static void bh_gl_set_viewport(BH_GPURenderPass *pass, const BH_GPUViewport *vp)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !vp)
        return;

    glViewport((GLint)vp->x, (GLint)vp->y, (GLsizei)vp->w, (GLsizei)vp->h);

    // Match BH_GPUViewport depth range semantics.
    // OpenGL default is [0,1], but callers may change it.
#ifdef __EMSCRIPTEN__
    glDepthRangef(vp->min_depth, vp->max_depth);
#else
    glDepthRange((GLdouble)vp->min_depth, (GLdouble)vp->max_depth);
#endif
}

static void bh_gl_set_scissor(BH_GPURenderPass *pass, const BH_GPU_Rect *r)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !r)
        return;

    if (!p->dev->scissor_enabled)
    {
        glEnable(GL_SCISSOR_TEST);
        p->dev->scissor_enabled = true;
    }
    glScissor(r->x, r->y, r->w, r->h);
}

static void bh_gl_bind_graphics_pipeline(BH_GPURenderPass *pass, BH_GPUGraphicsPipeline *pipeline)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    BH_GLPipeline *pl = (BH_GLPipeline *)pipeline;
    if (!p || !pl)
        return;

    p->pipeline = pl;
    p->gl_prim = bh_gl_prim_mode(pl->primitive);

    glUseProgram(pl->program);
    glBindVertexArray(pl->vao);

    bh_gl_apply_pipeline_state(p->dev, pl);

    // IMPORTANT: make binding order independent
    bh_gl_apply_vertex_bindings(p);
    bh_gl_apply_index_binding(p);
}

static void bh_gl_bind_vertex_buffers(BH_GPURenderPass *pass, uint32_t first_slot, const BH_GPUBufferBinding *bindings,
                                      uint32_t num_bindings)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !bindings || num_bindings == 0)
        return;

    // Cache buffer bindings regardless of whether a pipeline is bound yet.
    const uint32_t max_slots = (uint32_t)(sizeof(p->vertex_buffers) / sizeof(p->vertex_buffers[0]));

    for (uint32_t i = 0; i < num_bindings; ++i)
    {
        const uint32_t slot = first_slot + i;
        if (slot >= max_slots)
            continue;

        p->vertex_buffers[slot] = (BH_GLBuffer *)bindings[i].buffer;
        p->vertex_offsets[slot] = bindings[i].offset;
    }

    // If a pipeline/VAO is already bound, apply immediately.
    if (p->pipeline)
        bh_gl_apply_vertex_bindings(p);
}

static void bh_gl_bind_index_buffer(BH_GPURenderPass *pass, const BH_GPUBufferBinding *binding,
                                    uint32_t index_element_size)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !binding)
        return;

    p->index_buffer = (BH_GLBuffer *)binding->buffer;
    const bool is_u16 = (index_element_size == BH_GPU_INDEXELEMENTSIZE_16BIT);
    p->index_element_size = is_u16 ? 2u : 4u;
    p->index_type = is_u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    if (p->pipeline && p->index_buffer)
        bh_gl_apply_index_binding(p);
}

static void bh_gl_bind_fragment_samplers(BH_GPURenderPass *pass, uint32_t first_slot,
                                         const BH_GPUTextureSamplerBinding *bindings, uint32_t num_bindings)
{
    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !bindings)
        return;

    for (uint32_t i = 0; i < num_bindings; ++i)
    {
        uint32_t slot = first_slot + i;
        BH_GLTexture *tex = (BH_GLTexture *)bindings[i].texture;
        BH_GLSampler *samp = (BH_GLSampler *)bindings[i].sampler;
        if (!tex)
            continue;

        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(tex->target, tex->id);
        glBindSampler(slot, (samp && samp->id) ? samp->id : 0);
    }
}

static void bh_gl_draw_indexed_primitives(BH_GPURenderPass *pass, uint32_t index_count, uint32_t instance_count,
                                          uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    (void)first_instance;

    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !p->pipeline)
        return;

    const void *index_ptr = (const void *)(uintptr_t)(first_index * p->index_element_size);

#if defined(GL_VERSION_3_3)
    if (instance_count > 1)
    {
        glDrawElementsInstanced(p->gl_prim, (GLsizei)index_count, p->index_type, index_ptr, (GLsizei)instance_count);
        return;
    }
#endif
#if defined(GL_VERSION_3_2)
    if (vertex_offset != 0)
    {
        glDrawElementsBaseVertex(p->gl_prim, (GLsizei)index_count, p->index_type, index_ptr, (GLint)vertex_offset);
        return;
    }
#endif
    glDrawElements(p->gl_prim, (GLsizei)index_count, p->index_type, index_ptr);
}

static void bh_gl_draw_primitives(BH_GPURenderPass *pass, uint32_t vertex_count, uint32_t instance_count,
                                  uint32_t first_vertex, uint32_t first_instance)
{
    (void)first_instance;

    BH_GLRenderPass *p = (BH_GLRenderPass *)pass;
    if (!p || !p->pipeline)
        return;

#if defined(GL_VERSION_3_3)
    if (instance_count > 1)
    {
        glDrawArraysInstanced(p->gl_prim, (GLint)first_vertex, (GLsizei)vertex_count, (GLsizei)instance_count);
        return;
    }
#endif
    glDrawArrays(p->gl_prim, (GLint)first_vertex, (GLsizei)vertex_count);
}

static BH_GPUTexture *bh_gl_create_texture(BH_GPUDevice *device, const BH_GPUTextureCreateInfo *ci)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !ci)
        return NULL;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLTexture *tex = (BH_GLTexture *)SDL_calloc(1, sizeof(BH_GLTexture));
    if (!tex)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    tex->target = GL_TEXTURE_2D;
    tex->width = ci->width;
    tex->height = ci->height;

    // Format mapping
    switch ((GLenum)ci->format)
    {
    case GL_RGBA8:
        tex->internal_format = GL_RGBA8;
        tex->upload_format = GL_RGBA;
        tex->upload_type = GL_UNSIGNED_BYTE;
        tex->is_depth = false;
        break;
    case GL_SRGB8_ALPHA8:
        tex->internal_format = GL_SRGB8_ALPHA8;
        tex->upload_format = GL_RGBA;
        tex->upload_type = GL_UNSIGNED_BYTE;
        tex->is_depth = false;
        break;
    case GL_DEPTH_COMPONENT32F:
        tex->internal_format = GL_DEPTH_COMPONENT32F;
        tex->upload_format = GL_DEPTH_COMPONENT;
        tex->upload_type = GL_FLOAT;
        tex->is_depth = true;
        break;
    default:
        if (ci->format == BH_GPU_GetTextureFormat_R8G8B8A8_UNORM())
        {
            tex->internal_format = GL_RGBA8;
            tex->upload_format = GL_RGBA;
            tex->upload_type = GL_UNSIGNED_BYTE;
            tex->is_depth = false;
        }
        else if (ci->format == BH_GPU_GetTextureFormat_R8G8B8A8_UNORM_SRGB())
        {
            tex->internal_format = GL_SRGB8_ALPHA8;
            tex->upload_format = GL_RGBA;
            tex->upload_type = GL_UNSIGNED_BYTE;
            tex->is_depth = false;
        }
        else if (ci->format == BH_GPU_GetTextureFormat_D32_FLOAT())
        {
            tex->internal_format = GL_DEPTH_COMPONENT32F;
            tex->upload_format = GL_DEPTH_COMPONENT;
            tex->upload_type = GL_FLOAT;
            tex->is_depth = true;
        }
        else
        {
            bh_gl_set_error("OpenGL: unsupported texture format (%u)", ci->format);
            SDL_free(tex);
            return NULL;
        }
        break;
    }

    glGenTextures(1, &tex->id);
    glBindTexture(GL_TEXTURE_2D, tex->id);

    // Defaults; can be overridden by sampler objects.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    if (tex->is_depth)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)tex->internal_format, (GLsizei)ci->width, (GLsizei)ci->height, 0,
                 tex->upload_format, tex->upload_type, NULL);

    return (BH_GPUTexture *)tex;
}

static void bh_gl_release_texture(BH_GPUDevice *device, BH_GPUTexture *texture)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    BH_GLTexture *tex = (BH_GLTexture *)texture;
    if (!dev || !tex)
        return;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    if (tex->id && !tex->is_swapchain)
    {
        glDeleteTextures(1, &tex->id);
    }
    SDL_free(tex);
}

static BH_GPUBuffer *bh_gl_create_buffer(BH_GPUDevice *device, const BH_GPUBufferCreateInfo *ci)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !ci)
        return NULL;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLBuffer *b = (BH_GLBuffer *)SDL_calloc(1, sizeof(BH_GLBuffer));
    if (!b)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    b->size = ci->size;
    b->target = (ci->usage & BH_GPU_BUFFERUSAGE_INDEX) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;

    glGenBuffers(1, &b->id);

    glBindVertexArray(0);

    GLenum usage = (ci->usage & BH_GPU_BUFFERUSAGE_DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBindBuffer(b->target, b->id);
    glBufferData(b->target, (GLsizeiptr)ci->size, NULL, usage);

    // Unbind immediately to keep state clean
    glBindBuffer(b->target, 0);

    return (BH_GPUBuffer *)b;
}

static void bh_gl_release_buffer(BH_GPUDevice *device, BH_GPUBuffer *buffer)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    BH_GLBuffer *b = (BH_GLBuffer *)buffer;
    if (!dev || !b)
        return;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    if (b->id)
        glDeleteBuffers(1, &b->id);
    SDL_free(b);
}

static BH_GPUTransferBuffer *bh_gl_create_transfer_buffer(BH_GPUDevice *device,
                                                          const BH_GPUTransferBufferCreateInfo *ci)
{
    (void)device;
    if (!ci)
        return NULL;

    BH_GLTransferBuffer *tb = (BH_GLTransferBuffer *)SDL_calloc(1, sizeof(BH_GLTransferBuffer));
    if (!tb)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    tb->size = ci->size;
    tb->data = SDL_malloc((size_t)ci->size);
    if (!tb->data)
    {
        SDL_free(tb);
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    return (BH_GPUTransferBuffer *)tb;
}

static void bh_gl_release_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    (void)device;
    BH_GLTransferBuffer *tb = (BH_GLTransferBuffer *)buffer;
    if (!tb)
        return;

    if (tb->data)
        SDL_free(tb->data);
    SDL_free(tb);
}

static void *bh_gl_map_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer, bool cycle)
{
    (void)device;
    (void)cycle;
    BH_GLTransferBuffer *tb = (BH_GLTransferBuffer *)buffer;
    if (!tb)
        return NULL;
    return tb->data;
}

static void bh_gl_unmap_transfer_buffer(BH_GPUDevice *device, BH_GPUTransferBuffer *buffer)
{
    (void)device;
    (void)buffer;
}

static void bh_gl_upload_to_texture(BH_GPUCopyPass *pass, const BH_GPUTextureTransferInfo *src,
                                    const BH_GPUTextureRegion *dst, bool cycle)
{
    (void)cycle;

    BH_GLCopyPass *p = (BH_GLCopyPass *)pass;
    if (!p || !p->cmd || !p->cmd->dev || !src || !dst)
        return;

    BH_GLDevice *dev = p->cmd->dev;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLTransferBuffer *tb = (BH_GLTransferBuffer *)src->transfer_buffer;
    BH_GLTexture *tex = (BH_GLTexture *)dst->texture;
    if (!tb || !tex || tex->is_swapchain)
        return;

    const uint8_t *bytes = (const uint8_t *)tb->data;
    const void *data_ptr = (const void *)(bytes + src->offset);

    glBindTexture(tex->target, tex->id);

    // Handle row pitch for tightly packed uploads.
    if (src->pixels_per_row != 0)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)src->pixels_per_row);
    }
    else
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    glTexSubImage2D(tex->target, (GLint)dst->mip_level, (GLint)dst->x, (GLint)dst->y, (GLsizei)dst->w, (GLsizei)dst->h,
                    tex->upload_format, tex->upload_type, data_ptr);

    // Reset state
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void bh_gl_upload_to_buffer(BH_GPUCopyPass *pass, const BH_GPUTransferBufferLocation *src,
                                   const BH_GPUBufferRegion *dst, bool cycle)
{
    (void)cycle;

    BH_GLCopyPass *p = (BH_GLCopyPass *)pass;
    if (!p || !p->cmd || !p->cmd->dev || !src || !dst)
        return;

    BH_GLDevice *dev = p->cmd->dev;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLTransferBuffer *tb = (BH_GLTransferBuffer *)src->transfer_buffer;
    BH_GLBuffer *b = (BH_GLBuffer *)dst->buffer;
    if (!tb || !b)
        return;

    const uint8_t *bytes = (const uint8_t *)tb->data;
    const void *data_ptr = (const void *)(bytes + src->offset);

    glBindVertexArray(0);
    glBindBuffer(b->target, b->id);
    glBufferSubData(b->target, (GLintptr)dst->offset, (GLsizeiptr)dst->size, data_ptr);
    glBindBuffer(b->target, 0);
}

static BH_GPUSampler *bh_gl_create_sampler(BH_GPUDevice *device, const BH_GPUSamplerCreateInfo *ci)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !ci)
        return NULL;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLSampler *s = (BH_GLSampler *)SDL_calloc(1, sizeof(BH_GLSampler));
    if (!s)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    glGenSamplers(1, &s->id);

    // Filtering
    GLint mag = (ci->mag_filter == BH_GPU_FILTER_NEAREST) ? GL_NEAREST : GL_LINEAR;
    GLint min = (ci->min_filter == BH_GPU_FILTER_NEAREST) ? GL_NEAREST : GL_LINEAR;

    GLint min_mip;
    if (ci->mipmap_mode == BH_GPU_SAMPLERMIPMAPMODE_NEAREST)
        min_mip = (min == GL_NEAREST) ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_NEAREST;
    else
        min_mip = (min == GL_NEAREST) ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR;

    glSamplerParameteri(s->id, GL_TEXTURE_MAG_FILTER, mag);
    glSamplerParameteri(s->id, GL_TEXTURE_MIN_FILTER, min_mip);

    // Wrap
    GLint wrap_u = (ci->address_mode_u == BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLint wrap_v = (ci->address_mode_v == BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glSamplerParameteri(s->id, GL_TEXTURE_WRAP_S, wrap_u);
    glSamplerParameteri(s->id, GL_TEXTURE_WRAP_T, wrap_v);

    // Compare (shadow samplers) - optional
    if (ci->compare_op != BH_GPU_COMPAREOP_INVALID)
    {
        glSamplerParameteri(s->id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(s->id, GL_TEXTURE_COMPARE_FUNC, (GLint)bh_gl_compare(ci->compare_op));
    }
    else
    {
        glSamplerParameteri(s->id, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }

    return (BH_GPUSampler *)s;
}

static void bh_gl_release_sampler(BH_GPUDevice *device, BH_GPUSampler *sampler)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    BH_GLSampler *s = (BH_GLSampler *)sampler;
    if (!dev || !s)
        return;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    if (s->id)
        glDeleteSamplers(1, &s->id);
    SDL_free(s);
}

static BH_GPUShader *bh_gl_create_shader(BH_GPUDevice *device, const BH_GPUShaderCreateInfo *ci)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !ci || !ci->code || ci->code_size == 0)
    {
        bh_gl_set_error("OpenGL: create_shader invalid args");
        return NULL;
    }

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    if (ci->format != BH_GPU_SHADERFORMAT_GLSL)
    {
        bh_gl_set_error("OpenGL: only GLSL shader format is supported");
        return NULL;
    }

    GLenum stage = GL_VERTEX_SHADER;
    switch (ci->stage)
    {
    case BH_GPU_SHADERSTAGE_VERTEX:
        stage = GL_VERTEX_SHADER;
        break;
    case BH_GPU_SHADERSTAGE_FRAGMENT:
        stage = GL_FRAGMENT_SHADER;
        break;
    default:
        bh_gl_set_error("OpenGL: unsupported shader stage");
        return NULL;
    }

    GLuint sh = glCreateShader(stage);
    if (!sh)
    {
        bh_gl_set_error("OpenGL: glCreateShader failed");
        return NULL;
    }

    const GLchar *src = (const GLchar *)ci->code;
    GLint len = (GLint)ci->code_size;
    glShaderSource(sh, 1, &src, &len);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char logbuf[4096];
        GLsizei out_len = 0;
        glGetShaderInfoLog(sh, (GLsizei)sizeof(logbuf), &out_len, logbuf);
        logbuf[(out_len > 0 && out_len < (GLsizei)sizeof(logbuf)) ? out_len : (GLsizei)sizeof(logbuf) - 1] = '\0';
        bh_gl_set_error("OpenGL: shader compile failed: %s", logbuf);
        glDeleteShader(sh);
        return NULL;
    }

    BH_GLShader *shader = (BH_GLShader *)SDL_calloc(1, sizeof(BH_GLShader));
    if (!shader)
    {
        glDeleteShader(sh);
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    shader->id = sh;
    shader->stage = stage;
    shader->format = ci->format;
    return (BH_GPUShader *)shader;
}

static void bh_gl_release_shader(BH_GPUDevice *device, BH_GPUShader *shader)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    BH_GLShader *s = (BH_GLShader *)shader;
    if (!dev || !s)
        return;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    if (s->id)
        glDeleteShader(s->id);
    SDL_free(s);
}

static void bh_gl_program_bind_uniform_blocks(GLuint program)
{
    // Bind by convention: VS_UBO0.. and FS_UBO0..
    for (uint32_t slot = 0; slot < BH_GPU_MAX_UNIFORM_SLOTS; ++slot)
    {
        char name[32];

        SDL_snprintf(name, (int)sizeof(name), "VS_UBO%u", slot);
        GLuint idx_vs = glGetUniformBlockIndex(program, name);
        if (idx_vs != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(program, idx_vs, BH_GL_VS_UBO_BIND_BASE + slot);
        }

        SDL_snprintf(name, (int)sizeof(name), "FS_UBO%u", slot);
        GLuint idx_fs = glGetUniformBlockIndex(program, name);
        if (idx_fs != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(program, idx_fs, BH_GL_FS_UBO_BIND_BASE + slot);
        }
    }
}

static void bh_gl_program_bind_samplers(GLuint program)
{
    // Bind sampler array u_Tex[0..7] to texture units 0..7.
    GLint loc = glGetUniformLocation(program, "u_Tex[0]");
    if (loc >= 0)
    {
        GLint units[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        glUseProgram(program);
        glUniform1iv(loc, 8, units);
    }
}

static BH_GPUGraphicsPipeline *bh_gl_create_graphics_pipeline(BH_GPUDevice *device,
                                                              const BH_GPUGraphicsPipelineCreateInfo *ci)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    if (!dev || !ci || !ci->vertex_shader || !ci->fragment_shader)
    {
        bh_gl_set_error("OpenGL: create_graphics_pipeline invalid args");
        return NULL;
    }

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);

    BH_GLPipeline *p = (BH_GLPipeline *)SDL_calloc(1, sizeof(BH_GLPipeline));
    if (!p)
    {
        bh_gl_set_error("OpenGL: out of memory");
        return NULL;
    }

    BH_GLShader *vs = (BH_GLShader *)ci->vertex_shader;
    BH_GLShader *fs = (BH_GLShader *)ci->fragment_shader;

    p->program = glCreateProgram();
    if (!p->program)
    {
        SDL_free(p);
        bh_gl_set_error("OpenGL: glCreateProgram failed");
        return NULL;
    }

    glAttachShader(p->program, vs->id);
    glAttachShader(p->program, fs->id);

    glBindAttribLocation(p->program, 0, "a_Position");
    glBindAttribLocation(p->program, 1, "a_Normal");
    glBindAttribLocation(p->program, 2, "a_UV");
    glBindAttribLocation(p->program, 3, "a_UV2");

    glLinkProgram(p->program);

    GLint ok = 0;
    glGetProgramiv(p->program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char logbuf[4096];
        GLsizei out_len = 0;
        glGetProgramInfoLog(p->program, (GLsizei)sizeof(logbuf), &out_len, logbuf);
        logbuf[(out_len > 0 && out_len < (GLsizei)sizeof(logbuf)) ? out_len : (GLsizei)sizeof(logbuf) - 1] = '\0';
        bh_gl_set_error("OpenGL: program link failed: %s", logbuf);
        glDeleteProgram(p->program);
        SDL_free(p);
        return NULL;
    }

    // Bind UBOs and samplers by convention.
    bh_gl_program_bind_uniform_blocks(p->program);
    bh_gl_program_bind_samplers(p->program);

    // State
    p->primitive = ci->primitive_type;
    p->depth = ci->depth_stencil_state;
    p->rast = ci->rasterizer_state;

    // Blend state comes from the first color target description.
    // If none is provided, fall back to a sensible default.
    p->blend = (BH_GPUColorTargetBlendState){0};
    if (ci->target_info.color_target_descriptions && ci->target_info.num_color_targets > 0)
    {
        p->blend = ci->target_info.color_target_descriptions[0].blend_state;
    }

    // Vertex input state.
    //
    // NOTE: Unlike the SDL_gpu backend, the OpenGL backend uses the vertex input
    // state during draw-time VAO setup. Many callers provide these arrays as
    // stack temporaries when building pipeline create infos, so we must deep-copy
    // them here.
    p->vis = (BH_GPUVertexInputState){0};
    p->owned_vb_descs = NULL;
    p->owned_num_vb_descs = 0;
    p->owned_vertex_attrs = NULL;
    p->owned_num_vertex_attrs = 0;

    p->vis.num_vertex_buffers = ci->vertex_input_state.num_vertex_buffers;
    p->vis.num_vertex_attributes = ci->vertex_input_state.num_vertex_attributes;

    if (p->vis.num_vertex_buffers > 0)
    {
        if (!ci->vertex_input_state.vertex_buffer_descriptions)
        {
            bh_gl_set_error("OpenGL: pipeline vertex_buffer_descriptions is NULL");
            glDeleteProgram(p->program);
            SDL_free(p);
            return NULL;
        }

        p->owned_vb_descs = (BH_GPUVertexBufferDescription *)SDL_malloc(sizeof(BH_GPUVertexBufferDescription) *
                                                                        p->vis.num_vertex_buffers);
        if (!p->owned_vb_descs)
        {
            bh_gl_set_error("OpenGL: out of memory (vertex_buffer_descriptions)");
            glDeleteProgram(p->program);
            SDL_free(p);
            return NULL;
        }

        SDL_memcpy(p->owned_vb_descs, ci->vertex_input_state.vertex_buffer_descriptions,
                   sizeof(BH_GPUVertexBufferDescription) * p->vis.num_vertex_buffers);
        p->owned_num_vb_descs = p->vis.num_vertex_buffers;
        p->vis.vertex_buffer_descriptions = p->owned_vb_descs;
    }

    if (p->vis.num_vertex_attributes > 0)
    {
        if (!ci->vertex_input_state.vertex_attributes)
        {
            bh_gl_set_error("OpenGL: pipeline vertex_attributes is NULL");
            if (p->owned_vb_descs)
                SDL_free(p->owned_vb_descs);
            glDeleteProgram(p->program);
            SDL_free(p);
            return NULL;
        }

        p->owned_vertex_attrs =
            (BH_GPUVertexAttribute *)SDL_malloc(sizeof(BH_GPUVertexAttribute) * p->vis.num_vertex_attributes);
        if (!p->owned_vertex_attrs)
        {
            bh_gl_set_error("OpenGL: out of memory (vertex_attributes)");
            if (p->owned_vb_descs)
                SDL_free(p->owned_vb_descs);
            glDeleteProgram(p->program);
            SDL_free(p);
            return NULL;
        }

        SDL_memcpy(p->owned_vertex_attrs, ci->vertex_input_state.vertex_attributes,
                   sizeof(BH_GPUVertexAttribute) * p->vis.num_vertex_attributes);
        p->owned_num_vertex_attrs = p->vis.num_vertex_attributes;
        p->vis.vertex_attributes = p->owned_vertex_attrs;
    }

    glGenVertexArrays(1, &p->vao);

    return (BH_GPUGraphicsPipeline *)p;
}

static void bh_gl_release_graphics_pipeline(BH_GPUDevice *device, BH_GPUGraphicsPipeline *pipeline)
{
    BH_GLDevice *dev = (BH_GLDevice *)device;
    BH_GLPipeline *p = (BH_GLPipeline *)pipeline;
    if (!dev || !p)
        return;

    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    if (p->vao)
        glDeleteVertexArrays(1, &p->vao);
    if (p->program)
        glDeleteProgram(p->program);

    if (p->owned_vb_descs)
        SDL_free(p->owned_vb_descs);
    if (p->owned_vertex_attrs)
        SDL_free(p->owned_vertex_attrs);

    SDL_free(p);
}

static void bh_gl_push_uniform(BH_GLDevice *dev, BH_GLUniformBuffer *ubo, GLuint binding_point, const void *data,
                               uint32_t size)
{
    if (!ubo->id)
    {
        glGenBuffers(1, &ubo->id);
        ubo->capacity = 0;
    }

    glBindBuffer(GL_UNIFORM_BUFFER, ubo->id);
    if (size > ubo->capacity)
    {
        glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)size, data, GL_DYNAMIC_DRAW);
        ubo->capacity = size;
    }
    else
    {
        glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)size, data);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, binding_point, ubo->id);
}

static void bh_gl_push_vertex_uniform_data(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev || slot >= BH_GPU_MAX_UNIFORM_SLOTS || !data || size == 0)
        return;

    BH_GLDevice *dev = c->dev;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    bh_gl_push_uniform(dev, &dev->vs_ubos[slot], BH_GL_VS_UBO_BIND_BASE + slot, data, size);
}

static void bh_gl_push_fragment_uniform_data(BH_GPUCommandBuffer *cmd, uint32_t slot, const void *data, uint32_t size)
{
    BH_GLCommandBuffer *c = (BH_GLCommandBuffer *)cmd;
    if (!c || !c->dev || slot >= BH_GPU_MAX_UNIFORM_SLOTS || !data || size == 0)
        return;

    BH_GLDevice *dev = c->dev;
    (void)SDL_GL_MakeCurrent(dev->window, dev->glctx);
    bh_gl_push_uniform(dev, &dev->fs_ubos[slot], BH_GL_FS_UBO_BIND_BASE + slot, data, size);
}

static BH_GPUTextureFormat bh_gl_get_texture_format_r8g8b8a8_unorm(void)
{
    return (BH_GPUTextureFormat)GL_RGBA8;
}

static BH_GPUTextureFormat bh_gl_get_texture_format_r8g8b8a8_unorm_srgb(void)
{
    return (BH_GPUTextureFormat)GL_SRGB8_ALPHA8;
}

static BH_GPUTextureFormat bh_gl_get_texture_format_d32_float(void)
{
    return (BH_GPUTextureFormat)GL_DEPTH_COMPONENT32F;
}

static const char *bh_gl_get_last_error(void)
{
    return g_gl_last_error;
}

static const BH_GPUBackendVTable g_gl_vt = {
    .create_device = bh_gl_create_device,
    .destroy_device = bh_gl_destroy_device,

    .claim_window = bh_gl_claim_window,
    .release_window = bh_gl_release_window,

    .set_swapchain_parameters = bh_gl_set_swapchain_parameters,
    .get_swapchain_texture_format = bh_gl_get_swapchain_texture_format,

    .wait_for_idle = bh_gl_wait_for_idle,

    .acquire_command_buffer = bh_gl_acquire_command_buffer,
    .cancel_command_buffer = bh_gl_cancel_command_buffer,
    .submit_command_buffer = bh_gl_submit_command_buffer,

    .wait_and_acquire_swapchain_texture = bh_gl_wait_and_acquire_swapchain_texture,

    .begin_copy_pass = bh_gl_begin_copy_pass,
    .end_copy_pass = bh_gl_end_copy_pass,

    .begin_render_pass = bh_gl_begin_render_pass,
    .end_render_pass = bh_gl_end_render_pass,

    .set_viewport = bh_gl_set_viewport,
    .set_scissor = bh_gl_set_scissor,

    .bind_graphics_pipeline = bh_gl_bind_graphics_pipeline,
    .bind_vertex_buffers = bh_gl_bind_vertex_buffers,
    .bind_index_buffer = bh_gl_bind_index_buffer,
    .bind_fragment_samplers = bh_gl_bind_fragment_samplers,

    .draw_indexed_primitives = bh_gl_draw_indexed_primitives,
    .draw_primitives = bh_gl_draw_primitives,

    .create_texture = bh_gl_create_texture,
    .release_texture = bh_gl_release_texture,

    .create_buffer = bh_gl_create_buffer,
    .release_buffer = bh_gl_release_buffer,

    .create_transfer_buffer = bh_gl_create_transfer_buffer,
    .release_transfer_buffer = bh_gl_release_transfer_buffer,
    .map_transfer_buffer = bh_gl_map_transfer_buffer,
    .unmap_transfer_buffer = bh_gl_unmap_transfer_buffer,

    .upload_to_texture = bh_gl_upload_to_texture,
    .upload_to_buffer = bh_gl_upload_to_buffer,

    .create_sampler = bh_gl_create_sampler,
    .release_sampler = bh_gl_release_sampler,

    .create_shader = bh_gl_create_shader,
    .release_shader = bh_gl_release_shader,

    .create_graphics_pipeline = bh_gl_create_graphics_pipeline,
    .release_graphics_pipeline = bh_gl_release_graphics_pipeline,

    .push_vertex_uniform_data = bh_gl_push_vertex_uniform_data,
    .push_fragment_uniform_data = bh_gl_push_fragment_uniform_data,

    .get_texture_format_r8g8b8a8_unorm = bh_gl_get_texture_format_r8g8b8a8_unorm,
    .get_texture_format_r8g8b8a8_unorm_srgb = bh_gl_get_texture_format_r8g8b8a8_unorm_srgb,
    .get_texture_format_d32_float = bh_gl_get_texture_format_d32_float,

    .get_last_error = bh_gl_get_last_error,
};

const BH_GPUBackend BH_GPU_BACKEND_GL = {
    .name = "OpenGL",
    .vt = &g_gl_vt,
};
