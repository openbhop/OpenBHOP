#include "interface/bh_ui.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

/* We re-use the precompiled shaders from the upstream RmlUi SDL_GPU backend. */
#include "RmlUi_SDL_GPU/ShadersCompiledSPV.h"

// Forward declaration of the global struct so the namespace classes can use pointers to it.
struct BH_UI;

namespace
{

// -----------------------------
//  SDL-based SystemInterface
// -----------------------------

class BH_RmlSystemInterface final : public Rml::SystemInterface
{
  public:
    BH_RmlSystemInterface()
    {
        cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cursor_ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        cursor_resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        cursor_cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

        // Default.
        if (cursor_arrow)
            SDL_SetCursor(cursor_arrow);
    }

    ~BH_RmlSystemInterface() override
    {
        if (cursor_arrow)
            SDL_DestroyCursor(cursor_arrow);
        if (cursor_ibeam)
            SDL_DestroyCursor(cursor_ibeam);
        if (cursor_hand)
            SDL_DestroyCursor(cursor_hand);
        if (cursor_resize)
            SDL_DestroyCursor(cursor_resize);
        if (cursor_cross)
            SDL_DestroyCursor(cursor_cross);
    }

    double GetElapsedTime() override
    {
#if SDL_VERSION_ATLEAST(3, 0, 0)
        return static_cast<double>(SDL_GetTicks()) / 1000.0;
#else
        return static_cast<double>(SDL_GetTicks()) / 1000.0;
#endif
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        const char *prefix = "[rml]";
        switch (type)
        {
        case Rml::Log::LT_ERROR:
            prefix = "[rml][error]";
            break;
        case Rml::Log::LT_WARNING:
            prefix = "[rml][warn]";
            break;
        case Rml::Log::LT_ALWAYS:
            prefix = "[rml]";
            break;
        case Rml::Log::LT_INFO:
            prefix = "[rml][info]";
            break;
        case Rml::Log::LT_DEBUG:
            prefix = "[rml][debug]";
            break;
        default:
            break;
        }

        SDL_Log("%s %s", prefix, message.c_str());
        return true;
    }

    void SetMouseCursor(const Rml::String &cursor_name) override
    {
        SDL_Cursor *cursor = cursor_arrow;

        if (cursor_name == "pointer")
        {
            cursor = cursor_hand;
        }
        else if (cursor_name == "text")
        {
            cursor = cursor_ibeam;
        }
        else if (cursor_name == "crosshair")
        {
            cursor = cursor_cross;
        }
        else if (cursor_name == "resize")
        {
            cursor = cursor_resize;
        }
        else
        {
            cursor = cursor_arrow;
        }

        if (cursor)
            SDL_SetCursor(cursor);
    }

    void SetClipboardText(const Rml::String &text) override
    {
        (void)SDL_SetClipboardText(text.c_str());
    }

    void GetClipboardText(Rml::String &text) override
    {
        char *clip = SDL_GetClipboardText();
        if (!clip)
        {
            text.clear();
            return;
        }

        text = clip;
        SDL_free(clip);
    }

  private:
    SDL_Cursor *cursor_arrow = nullptr;
    SDL_Cursor *cursor_ibeam = nullptr;
    SDL_Cursor *cursor_hand = nullptr;
    SDL_Cursor *cursor_resize = nullptr;
    SDL_Cursor *cursor_cross = nullptr;
};

// -----------------------------
//  Asset-root FileInterface
// -----------------------------

static bool bh_path_is_absolute(const std::string &path)
{
#ifdef _WIN32
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return true;
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\')
        return true;
    if (!path.empty() && (path[0] == '/' || path[0] == '\\'))
        return true;
#else
    if (!path.empty() && path[0] == '/')
        return true;
#endif
    return false;
}

static std::string bh_path_join(const std::string &a, const std::string &b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;

    const char last = a.back();
    if (last == '/' || last == '\\')
        return a + b;
    return a + "/" + b;
}

class BH_RmlFileInterface final : public Rml::FileInterface
{
  public:
    explicit BH_RmlFileInterface(std::string asset_root) : asset_root_(std::move(asset_root))
    {
    }

    Rml::FileHandle Open(const Rml::String &path) override
    {
        std::string p = path;

        // Normalize backslashes.
        for (char &c : p)
        {
            if (c == '\\')
                c = '/';
        }

        std::string full = p;
        if (!bh_path_is_absolute(full))
        {
            full = bh_path_join(asset_root_, full);
        }

        FILE *f = std::fopen(full.c_str(), "rb");
        return reinterpret_cast<Rml::FileHandle>(f);
    }

    void Close(Rml::FileHandle file) override
    {
        FILE *f = reinterpret_cast<FILE *>(file);
        if (f)
            std::fclose(f);
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        FILE *f = reinterpret_cast<FILE *>(file);
        if (!f)
            return 0;
        return std::fread(buffer, 1, size, f);
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        FILE *f = reinterpret_cast<FILE *>(file);
        if (!f)
            return false;
        return std::fseek(f, offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        FILE *f = reinterpret_cast<FILE *>(file);
        if (!f)
            return 0;
        return static_cast<size_t>(std::ftell(f));
    }

  private:
    std::string asset_root_;
};

// -----------------------------
//  BH_GPU Render backend (backend-agnostic)
// -----------------------------

/*
    For the SDL3 backend, we re-use the precompiled SPIR-V shaders from the upstream
    RmlUi SDL_GPU renderer (ShadersCompiledSPV.h).

    For the OpenGL backend, BH_GPU currently only supports GLSL sources, so we provide
    minimal GLSL equivalents for the UI shaders here.
*/
static const char *g_rml_glsl_vert = R"(#version 300 es

// Attributes matching glBindAttribLocation in backend
in vec2 a_Position;
in vec4 a_Normal; // Color
in vec2 a_UV;

// We MUST keep std140 blocks here because bh_gpu_gl.c uses 
// glBufferSubData to upload to these slots.
layout(std140) uniform VS_UBO0
{
    mat4 Transform;
};

layout(std140) uniform VS_UBO1
{
    vec2 Translate;
    vec2 _Pad0; // Padding matches C-side structure alignment
};

out vec4 v_Color;
out vec2 v_TexCoord;

void main()
{
    v_TexCoord = a_UV;
    v_Color = a_Normal;

    // RmlUi 2D geometry is flat (Z=0).
    vec4 position = vec4(a_Position + Translate, 0.0, 1.0);
    gl_Position = Transform * position;
}
)";

static const char *g_rml_glsl_frag_color = R"(#version 300 es
precision mediump float;

in vec4 v_Color;
out vec4 o_Color;

void main()
{
    o_Color = v_Color;
}
)";

static const char *g_rml_glsl_frag_texture = R"(#version 300 es
precision mediump float;

in vec4 v_Color;
in vec2 v_TexCoord;

// bh_gpu_gl.c binds samplers to units 0-7, so this array is correct.
uniform sampler2D u_Tex[8];

out vec4 o_Color;

void main()
{
    o_Color = v_Color * texture(u_Tex[0], v_TexCoord);
}
)";

struct BH_RmlBuffer
{
    uint32_t usage = 0;
    int capacity = 0; // bytes
    bool in_use = false;

    BH_GPUTransferBuffer *transfer = nullptr;
    BH_GPUBuffer *buffer = nullptr;
};

struct BH_RmlGeometry
{
    BH_RmlBuffer *vertex_buffer = nullptr;
    BH_RmlBuffer *index_buffer = nullptr;
    int num_indices = 0;
};

struct BH_RmlCommand
{
    virtual ~BH_RmlCommand() = default;
    virtual void Execute(BH_GPUCommandBuffer *cmd, BH_GPURenderPass *pass) = 0;
};

class BH_RmlRenderBackend
{
  public:
    explicit BH_RmlRenderBackend(BH_Renderer *renderer)
    {
        device_ = renderer ? renderer->device : nullptr;
        window_ = renderer ? renderer->window : nullptr;
        color_format_ = renderer ? renderer->swapchain_format : (BH_GPUTextureFormat)0;
        depth_format_ = renderer ? renderer->depth_format : (BH_GPUTextureFormat)0;

        use_gl_ = (SDL_strcasecmp(BH_GPU_GetBackend()->name, "OpenGL") == 0);

        if (device_)
        {
            CreateDeviceObjects();
        }
    }

    ~BH_RmlRenderBackend()
    {
        DestroyDeviceObjects();
    }

    void SetPreparePass(BH_GPUCommandBuffer *cmd, BH_GPUCopyPass *copy_pass, uint32_t fb_w, uint32_t fb_h)
    {
        cmd_prepare_ = cmd;
        copy_pass_ = copy_pass;

        fb_w_ = fb_w;
        fb_h_ = fb_h;

        // Update ortho projection for the new framebuffer size.
        projection_ = Rml::Matrix4f::ProjectOrtho(0.f, float(fb_w), float(fb_h), 0.f, -10000.f, 10000.f);

        // Reset per-frame render state.
        transform_ = projection_;
        scissor_enabled_ = false;
        scissor_rect_ = {0, 0, int32_t(fb_w), int32_t(fb_h)};

        draw_commands_.clear();
    }

    void RenderToPass(BH_GPUCommandBuffer *cmd, BH_GPURenderPass *pass)
    {
        if (!device_ || !pass)
            return;

        // Bind a full-screen viewport.
        const BH_GPUViewport viewport{0.0f, 0.0f, float(fb_w_), float(fb_h_), 0.0f, 1.0f};
        BH_GPU_SetViewport(pass, &viewport);

        // Ensure scissor is set.
        BH_GPU_SetScissor(pass, &scissor_rect_);

        // Execute recorded draw commands.
        for (auto &c : draw_commands_)
        {
            c->Execute(cmd, pass);
        }
    }

    void EndFrame(bool /*submit_ok*/)
    {
        // Execute deferred releases (safe after submission attempt).
        for (auto &fn : pending_end_frame_)
        {
            fn();
        }
        pending_end_frame_.clear();

        // Reset per-frame handles.
        cmd_prepare_ = nullptr;
        copy_pass_ = nullptr;
    }

    // ---- RenderInterface-like entrypoints ----

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
    {
        if (!device_)
        {
            return 0;
        }

        const int vertex_bytes = int(vertices.size()) * int(sizeof(Rml::Vertex));
        const int index_bytes = int(indices.size()) * int(sizeof(int));

        BH_RmlBuffer *vb = AcquireBuffer(vertex_bytes, BH_GPU_BUFFERUSAGE_VERTEX | BH_GPU_BUFFERUSAGE_DYNAMIC);
        BH_RmlBuffer *ib = AcquireBuffer(index_bytes, BH_GPU_BUFFERUSAGE_INDEX | BH_GPU_BUFFERUSAGE_DYNAMIC);

        if (!vb || !ib)
            return 0;

        // Upload via current copy pass if present, otherwise do an immediate one-off submit.
        BH_GPUCommandBuffer *upload_cmd = cmd_prepare_;
        BH_GPUCopyPass *upload_copy = copy_pass_;

        BH_GPUCommandBuffer *temp_cmd = nullptr;
        BH_GPUCopyPass *temp_copy = nullptr;

        if (!upload_cmd || !upload_copy)
        {
            temp_cmd = BH_GPU_AcquireCommandBuffer(device_);
            if (!temp_cmd)
                return 0;
            temp_copy = BH_GPU_BeginCopyPass(temp_cmd);
            if (!temp_copy)
            {
                BH_GPU_CancelCommandBuffer(temp_cmd);
                return 0;
            }
            upload_cmd = temp_cmd;
            upload_copy = temp_copy;
        }

        // Vertex data
        {
            void *mapped = BH_GPU_MapTransferBuffer(device_, vb->transfer, false);
            if (!mapped)
            {
                if (temp_copy)
                    BH_GPU_EndCopyPass(temp_copy);
                if (temp_cmd)
                    BH_GPU_CancelCommandBuffer(temp_cmd);
                return 0;
            }

            std::memcpy(mapped, vertices.data(), size_t(vertex_bytes));
            BH_GPU_UnmapTransferBuffer(device_, vb->transfer);

            BH_GPUTransferBufferLocation src{};
            src.transfer_buffer = vb->transfer;
            src.offset = 0;

            BH_GPUBufferRegion dst{};
            dst.buffer = vb->buffer;
            dst.offset = 0;
            dst.size = uint32_t(vertex_bytes);

            BH_GPU_UploadToBuffer(upload_copy, &src, &dst, false);
        }

        // Index data
        {
            void *mapped = BH_GPU_MapTransferBuffer(device_, ib->transfer, false);
            if (!mapped)
            {
                if (temp_copy)
                    BH_GPU_EndCopyPass(temp_copy);
                if (temp_cmd)
                    BH_GPU_CancelCommandBuffer(temp_cmd);
                return 0;
            }

            std::memcpy(mapped, indices.data(), size_t(index_bytes));
            BH_GPU_UnmapTransferBuffer(device_, ib->transfer);

            BH_GPUTransferBufferLocation src{};
            src.transfer_buffer = ib->transfer;
            src.offset = 0;

            BH_GPUBufferRegion dst{};
            dst.buffer = ib->buffer;
            dst.offset = 0;
            dst.size = uint32_t(index_bytes);

            BH_GPU_UploadToBuffer(upload_copy, &src, &dst, false);
        }

        if (temp_copy)
        {
            BH_GPU_EndCopyPass(temp_copy);
            const bool ok = BH_GPU_SubmitCommandBuffer(temp_cmd);
            if (ok)
            {
                BH_GPU_WaitForIdle(device_);
            }
            else
            {
                // Upload failed; mark buffers reusable.
                vb->in_use = false;
                ib->in_use = false;
                return 0;
            }
        }

        auto *geom = new BH_RmlGeometry;
        geom->vertex_buffer = vb;
        geom->index_buffer = ib;
        geom->num_indices = int(indices.size());

        return reinterpret_cast<Rml::CompiledGeometryHandle>(geom);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
    {
        if (!device_)
            return;

        BH_RmlGeometry *geom = reinterpret_cast<BH_RmlGeometry *>(geometry);
        if (!geom)
            return;

        struct Cmd final : BH_RmlCommand
        {
            BH_RmlRenderBackend *backend = nullptr;
            BH_RmlGeometry *geom = nullptr;
            Rml::Vector2f translation{};
            BH_GPUTexture *texture = nullptr;

            void Execute(BH_GPUCommandBuffer *cmd, BH_GPURenderPass *pass) override
            {
                backend->DrawGeometry(cmd, pass, geom, translation, texture);
            }
        };

        std::unique_ptr<Cmd> cmd = std::make_unique<Cmd>();
        cmd->backend = this;
        cmd->geom = geom;
        cmd->translation = translation;
        cmd->texture = reinterpret_cast<BH_GPUTexture *>(texture);
        draw_commands_.push_back(std::move(cmd));
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        BH_RmlGeometry *geom = reinterpret_cast<BH_RmlGeometry *>(geometry);
        if (!geom)
            return;

        pending_end_frame_.push_back([this, geom]() {
            if (geom->vertex_buffer)
                geom->vertex_buffer->in_use = false;
            if (geom->index_buffer)
                geom->index_buffer->in_use = false;
            delete geom;
        });
    }

    bool LoadTexture(Rml::TextureHandle &texture_handle, Rml::Vector2i &texture_dimensions,
                     const Rml::String & /*source*/)
    {
        // Basic integration: fonts and most UI elements rely on GenerateTexture (used by the font engine).
        // Image loading can be added later (e.g. stb_image or SDL_image).
        texture_handle = 0;
        texture_dimensions = Rml::Vector2i(0, 0);
        return false;
    }

    bool GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source, const Rml::Vector2i &dimensions)
    {
        if (!device_ || !source || dimensions.x <= 0 || dimensions.y <= 0)
        {
            texture_handle = 0;
            return false;
        }

        BH_GPUTextureCreateInfo tex_ci{};
        tex_ci.type = BH_GPU_TEXTURETYPE_2D;
        tex_ci.format = BH_GPU_GetTextureFormat_R8G8B8A8_UNORM();
        tex_ci.width = uint32_t(dimensions.x);
        tex_ci.height = uint32_t(dimensions.y);
        tex_ci.layer_count_or_depth = 1;
        tex_ci.num_levels = 1;
        tex_ci.sample_count = BH_GPU_SAMPLECOUNT_1;
        tex_ci.usage = BH_GPU_TEXTUREUSAGE_SAMPLER;

        BH_GPUTexture *texture = BH_GPU_CreateTexture(device_, &tex_ci);
        if (!texture)
        {
            texture_handle = 0;
            return false;
        }

        const int pitch = dimensions.x * 4;
        const int upload_bytes = pitch * dimensions.y;

        auto do_upload = [&](BH_GPUCommandBuffer * /*cmd*/, BH_GPUCopyPass *copy) {
            BH_GPUTransferBufferCreateInfo tci{};
            tci.usage = BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tci.size = uint32_t(upload_bytes);

            BH_GPUTransferBuffer *tbuf = BH_GPU_CreateTransferBuffer(device_, &tci);
            if (!tbuf)
                return false;

            void *mapped = BH_GPU_MapTransferBuffer(device_, tbuf, false);
            if (!mapped)
            {
                BH_GPU_ReleaseTransferBuffer(device_, tbuf);
                return false;
            }

            std::memcpy(mapped, source, size_t(upload_bytes));
            BH_GPU_UnmapTransferBuffer(device_, tbuf);

            BH_GPUTextureTransferInfo src_info{};
            src_info.transfer_buffer = tbuf;
            src_info.offset = 0;
            // RmlUi textures are tightly packed, so pixels_per_row is just the width.
            src_info.pixels_per_row = uint32_t(dimensions.x);
            src_info.rows_per_layer = uint32_t(dimensions.y);

            BH_GPUTextureRegion dst_region{};
            dst_region.texture = texture;
            dst_region.mip_level = 0;
            dst_region.layer = 0;
            dst_region.x = 0;
            dst_region.y = 0;
            dst_region.z = 0;
            dst_region.w = uint32_t(dimensions.x);
            dst_region.h = uint32_t(dimensions.y);
            dst_region.d = 1;

            BH_GPU_UploadToTexture(copy, &src_info, &dst_region, false);

            BH_GPU_ReleaseTransferBuffer(device_, tbuf);
            return true;
        };

        if (cmd_prepare_ && copy_pass_)
        {
            if (!do_upload(cmd_prepare_, copy_pass_))
            {
                BH_GPU_ReleaseTexture(device_, texture);
                texture_handle = 0;
                return false;
            }
        }
        else
        {
            BH_GPUCommandBuffer *cmd = BH_GPU_AcquireCommandBuffer(device_);
            if (!cmd)
            {
                BH_GPU_ReleaseTexture(device_, texture);
                texture_handle = 0;
                return false;
            }
            BH_GPUCopyPass *copy = BH_GPU_BeginCopyPass(cmd);
            if (!copy)
            {
                BH_GPU_CancelCommandBuffer(cmd);
                BH_GPU_ReleaseTexture(device_, texture);
                texture_handle = 0;
                return false;
            }

            const bool ok = do_upload(cmd, copy);
            BH_GPU_EndCopyPass(copy);
            if (!ok)
            {
                BH_GPU_CancelCommandBuffer(cmd);
                BH_GPU_ReleaseTexture(device_, texture);
                texture_handle = 0;
                return false;
            }

            const bool submit_ok = BH_GPU_SubmitCommandBuffer(cmd);
            if (submit_ok)
            {
                BH_GPU_WaitForIdle(device_);
            }
            else
            {
                BH_GPU_ReleaseTexture(device_, texture);
                texture_handle = 0;
                return false;
            }
        }

        texture_handle = reinterpret_cast<Rml::TextureHandle>(texture);
        return true;
    }

    void ReleaseTexture(Rml::TextureHandle texture)
    {
        BH_GPUTexture *tex = reinterpret_cast<BH_GPUTexture *>(texture);
        if (!tex)
            return;

        pending_end_frame_.push_back([this, tex]() { BH_GPU_ReleaseTexture(device_, tex); });
    }

    void EnableScissorRegion(bool enable)
    {
        struct Cmd final : BH_RmlCommand
        {
            BH_RmlRenderBackend *backend = nullptr;
            bool enable = false;
            void Execute(BH_GPUCommandBuffer * /*cmd*/, BH_GPURenderPass *pass) override
            {
                backend->scissor_enabled_ = enable;
                if (!backend->scissor_enabled_)
                {
                    backend->scissor_rect_ = {0, 0, int32_t(backend->fb_w_), int32_t(backend->fb_h_)};
                }
                BH_GPU_SetScissor(pass, &backend->scissor_rect_);
            }
        };

        std::unique_ptr<Cmd> cmd = std::make_unique<Cmd>();
        cmd->backend = this;
        cmd->enable = enable;
        draw_commands_.push_back(std::move(cmd));
    }

    void SetScissorRegion(const Rml::Rectanglei &region)
    {
        struct Cmd final : BH_RmlCommand
        {
            BH_RmlRenderBackend *backend = nullptr;
            BH_GPU_Rect rect{};
            void Execute(BH_GPUCommandBuffer * /*cmd*/, BH_GPURenderPass *pass) override
            {
                backend->scissor_rect_ = rect;
                BH_GPU_SetScissor(pass, &backend->scissor_rect_);
            }
        };

        std::unique_ptr<Cmd> cmd = std::make_unique<Cmd>();
        cmd->backend = this;

        BH_GPU_Rect r{};
        r.x = region.Left();
        r.y = region.Top();
        r.w = region.Width();
        r.h = region.Height();

        // RmlUi uses a top-left origin. OpenGL scissor uses bottom-left.
        if (use_gl_)
        {
            r.y = int32_t(fb_h_) - (r.y + r.h);
        }

        cmd->rect = r;
        draw_commands_.push_back(std::move(cmd));
    }

    void SetTransform(const Rml::Matrix4f *transform)
    {
        struct Cmd final : BH_RmlCommand
        {
            BH_RmlRenderBackend *backend = nullptr;
            Rml::Matrix4f transform{};
            void Execute(BH_GPUCommandBuffer * /*cmd*/, BH_GPURenderPass * /*pass*/) override
            {
                backend->transform_ = backend->projection_ * transform;
            }
        };

        std::unique_ptr<Cmd> cmd = std::make_unique<Cmd>();
        cmd->backend = this;
        cmd->transform = transform ? *transform : Rml::Matrix4f::Identity();
        draw_commands_.push_back(std::move(cmd));
    }

  private:
    void CreateDeviceObjects()
    {
        if (!device_)
            return;

        BH_GPUSamplerCreateInfo sampler_info{};
        sampler_info.min_filter = BH_GPU_FILTER_NEAREST;
        sampler_info.mag_filter = BH_GPU_FILTER_NEAREST;
        sampler_info.mipmap_mode = BH_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler_info.address_mode_u = BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_info.address_mode_v = BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_info.address_mode_w = BH_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

        sampler_linear_ = BH_GPU_CreateSampler(device_, &sampler_info);

        BH_GPUShaderCreateInfo vs_ci{};
        vs_ci.entrypoint = "main";
        vs_ci.stage = BH_GPU_SHADERSTAGE_VERTEX;
        vs_ci.num_uniform_buffers = 2;

        BH_GPUShaderCreateInfo fs_color_ci{};
        fs_color_ci.entrypoint = "main";
        fs_color_ci.stage = BH_GPU_SHADERSTAGE_FRAGMENT;

        BH_GPUShaderCreateInfo fs_tex_ci{};
        fs_tex_ci.entrypoint = "main";
        fs_tex_ci.stage = BH_GPU_SHADERSTAGE_FRAGMENT;
        fs_tex_ci.num_samplers = 1;

        if (use_gl_)
        {
            vs_ci.code = g_rml_glsl_vert;
            vs_ci.code_size = (uint32_t)SDL_strlen(g_rml_glsl_vert);
            vs_ci.format = BH_GPU_SHADERFORMAT_GLSL;

            fs_color_ci.code = g_rml_glsl_frag_color;
            fs_color_ci.code_size = (uint32_t)SDL_strlen(g_rml_glsl_frag_color);
            fs_color_ci.format = BH_GPU_SHADERFORMAT_GLSL;

            fs_tex_ci.code = g_rml_glsl_frag_texture;
            fs_tex_ci.code_size = (uint32_t)SDL_strlen(g_rml_glsl_frag_texture);
            fs_tex_ci.format = BH_GPU_SHADERFORMAT_GLSL;
        }
        else
        {
            vs_ci.code = shader_vert_spirv;
            vs_ci.code_size = (uint32_t)sizeof(shader_vert_spirv);
            vs_ci.format = BH_GPU_SHADERFORMAT_SPIRV;

            fs_color_ci.code = shader_frag_color_spirv;
            fs_color_ci.code_size = (uint32_t)sizeof(shader_frag_color_spirv);
            fs_color_ci.format = BH_GPU_SHADERFORMAT_SPIRV;

            fs_tex_ci.code = shader_frag_texture_spirv;
            fs_tex_ci.code_size = (uint32_t)sizeof(shader_frag_texture_spirv);
            fs_tex_ci.format = BH_GPU_SHADERFORMAT_SPIRV;
        }

        vertex_shader_ = BH_GPU_CreateShader(device_, &vs_ci);
        fragment_color_shader_ = BH_GPU_CreateShader(device_, &fs_color_ci);
        fragment_texture_shader_ = BH_GPU_CreateShader(device_, &fs_tex_ci);

        if (!vertex_shader_ || !fragment_color_shader_ || !fragment_texture_shader_)
        {
            SDL_Log("[bh][rml] Failed to create UI shaders: %s", BH_GPU_GetLastError());
            return;
        }

        BH_GPUVertexBufferDescription vb_desc{};
        vb_desc.slot = 0;
        vb_desc.pitch = uint32_t(sizeof(Rml::Vertex));
        vb_desc.input_rate = BH_GPU_VERTEXINPUTRATE_VERTEX;
        vb_desc.instance_step_rate = 0;

        BH_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = 0;

        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = BH_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        attrs[1].offset = uint32_t(offsetof(Rml::Vertex, colour));

        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[2].offset = uint32_t(offsetof(Rml::Vertex, tex_coord));

        BH_GPUColorTargetBlendState blend{};
        blend.enable_blend = true;
        blend.src_color_blendfactor = BH_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = BH_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = BH_GPU_BLENDOP_ADD;
        blend.enable_color_write_mask = false;

        BH_GPUColorTargetDescription color_target{};
        color_target.format = color_format_;
        color_target.blend_state = blend;

        BH_GPUGraphicsPipelineCreateInfo ci{};
        ci.vertex_shader = vertex_shader_;
        ci.fragment_shader = fragment_color_shader_;

        ci.vertex_input_state.num_vertex_buffers = 1;
        ci.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
        ci.vertex_input_state.num_vertex_attributes = 3;
        ci.vertex_input_state.vertex_attributes = attrs;

        ci.primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST;

        ci.rasterizer_state.fill_mode = BH_GPU_FILLMODE_FILL;
        ci.rasterizer_state.cull_mode = BH_GPU_CULLMODE_NONE;
        ci.rasterizer_state.front_face = BH_GPU_FRONTFACE_COUNTER_CLOCKWISE;

        ci.multisample_state.sample_count = BH_GPU_SAMPLECOUNT_1;

        ci.depth_stencil_state.enable_depth_test = false;
        ci.depth_stencil_state.enable_depth_write = false;
        ci.depth_stencil_state.compare_op = BH_GPU_COMPAREOP_ALWAYS;

        ci.target_info.num_color_targets = 1;
        ci.target_info.color_target_descriptions = &color_target;

        if (depth_format_ != 0)
        {
            ci.target_info.has_depth_stencil_target = true;
            ci.target_info.depth_stencil_format = depth_format_;
        }

        pipeline_color_ = BH_GPU_CreateGraphicsPipeline(device_, &ci);

        ci.fragment_shader = fragment_texture_shader_;
        pipeline_texture_ = BH_GPU_CreateGraphicsPipeline(device_, &ci);

        if (!pipeline_color_ || !pipeline_texture_)
        {
            SDL_Log("[bh][rml] Failed to create UI pipelines: %s", BH_GPU_GetLastError());
        }
    }

    void DestroyDeviceObjects()
    {
        if (!device_)
            return;

        for (const auto &b : buffers_)
        {
            if (b->transfer)
                BH_GPU_ReleaseTransferBuffer(device_, b->transfer);
            if (b->buffer)
                BH_GPU_ReleaseBuffer(device_, b->buffer);
        }
        buffers_.clear();

        if (pipeline_color_)
            BH_GPU_ReleaseGraphicsPipeline(device_, pipeline_color_);
        if (pipeline_texture_)
            BH_GPU_ReleaseGraphicsPipeline(device_, pipeline_texture_);
        pipeline_color_ = nullptr;
        pipeline_texture_ = nullptr;

        if (vertex_shader_)
            BH_GPU_ReleaseShader(device_, vertex_shader_);
        if (fragment_color_shader_)
            BH_GPU_ReleaseShader(device_, fragment_color_shader_);
        if (fragment_texture_shader_)
            BH_GPU_ReleaseShader(device_, fragment_texture_shader_);
        vertex_shader_ = nullptr;
        fragment_color_shader_ = nullptr;
        fragment_texture_shader_ = nullptr;

        if (sampler_linear_)
            BH_GPU_ReleaseSampler(device_, sampler_linear_);
        sampler_linear_ = nullptr;
    }

    BH_RmlBuffer *AcquireBuffer(int required_bytes, uint32_t usage)
    {
        required_bytes = std::max(required_bytes, 1);

        for (const auto &b_ptr : buffers_)
        {
            if (!b_ptr->in_use && b_ptr->usage == usage && b_ptr->capacity >= required_bytes)
            {
                b_ptr->in_use = true;
                return b_ptr.get();
            }
        }

        BH_RmlBuffer b{};
        b.usage = usage;
        b.capacity = required_bytes;
        b.in_use = true;

        BH_GPUBufferCreateInfo buf_ci{};
        buf_ci.usage = usage;
        buf_ci.size = uint32_t(required_bytes);

        BH_GPUTransferBufferCreateInfo tci{};
        tci.usage = BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = uint32_t(required_bytes);

        b.transfer = BH_GPU_CreateTransferBuffer(device_, &tci);
        b.buffer = BH_GPU_CreateBuffer(device_, &buf_ci);

        if (!b.transfer || !b.buffer)
        {
            if (b.transfer)
                BH_GPU_ReleaseTransferBuffer(device_, b.transfer);
            if (b.buffer)
                BH_GPU_ReleaseBuffer(device_, b.buffer);
            return nullptr;
        }

        buffers_.push_back(std::make_unique<BH_RmlBuffer>(b));
        return buffers_.back().get();
    }

    void DrawGeometry(BH_GPUCommandBuffer *cmd, BH_GPURenderPass *pass, BH_RmlGeometry *geom,
                      Rml::Vector2f translation, BH_GPUTexture *texture)
    {
        if (!geom || !geom->vertex_buffer || !geom->index_buffer)
            return;

        BH_GPUBufferBinding vb{};
        vb.buffer = geom->vertex_buffer->buffer;
        vb.offset = 0;
        BH_GPU_BindVertexBuffers(pass, 0, &vb, 1);

        BH_GPUBufferBinding ib{};
        ib.buffer = geom->index_buffer->buffer;
        ib.offset = 0;
        BH_GPU_BindIndexBuffer(pass, &ib, BH_GPU_INDEXELEMENTSIZE_32BIT);

        // Uniforms: transform + translation.
        BH_GPU_PushVertexUniformData(cmd, 0, &transform_, uint32_t(sizeof(transform_)));

        if (use_gl_)
        {
            // std140 padding (vec2 + vec2)
            const float tr[4] = {translation.x, translation.y, 0.0f, 0.0f};
            BH_GPU_PushVertexUniformData(cmd, 1, tr, uint32_t(sizeof(tr)));
        }
        else
        {
            BH_GPU_PushVertexUniformData(cmd, 1, &translation, uint32_t(sizeof(translation)));
        }

        if (texture)
        {
            BH_GPU_BindGraphicsPipeline(pass, pipeline_texture_);

            BH_GPUTextureSamplerBinding sampler_binding{};
            sampler_binding.sampler = sampler_linear_;
            sampler_binding.texture = texture;
            BH_GPU_BindFragmentSamplers(pass, 0, &sampler_binding, 1);
        }
        else
        {
            BH_GPU_BindGraphicsPipeline(pass, pipeline_color_);
        }

        BH_GPU_DrawIndexedPrimitives(pass, uint32_t(geom->num_indices), 1, 0, 0, 0);
    }

  private:
    BH_GPUDevice *device_ = nullptr;
    BH_Window *window_ = nullptr;

    bool use_gl_ = false;

    BH_GPUTextureFormat color_format_ = 0;
    BH_GPUTextureFormat depth_format_ = 0;

    BH_GPUSampler *sampler_linear_ = nullptr;
    BH_GPUShader *vertex_shader_ = nullptr;
    BH_GPUShader *fragment_color_shader_ = nullptr;
    BH_GPUShader *fragment_texture_shader_ = nullptr;

    BH_GPUGraphicsPipeline *pipeline_color_ = nullptr;
    BH_GPUGraphicsPipeline *pipeline_texture_ = nullptr;

    BH_GPUCommandBuffer *cmd_prepare_ = nullptr;
    BH_GPUCopyPass *copy_pass_ = nullptr;

    uint32_t fb_w_ = 0;
    uint32_t fb_h_ = 0;

    Rml::Matrix4f projection_ = Rml::Matrix4f::Identity();
    Rml::Matrix4f transform_ = Rml::Matrix4f::Identity();

    bool scissor_enabled_ = false;
    BH_GPU_Rect scissor_rect_{};

    std::vector<std::unique_ptr<BH_RmlBuffer>> buffers_;
    std::vector<std::unique_ptr<BH_RmlCommand>> draw_commands_;

    std::vector<std::function<void()>> pending_end_frame_;
};


// -----------------------------
//  RenderInterface forwarder
// -----------------------------

class BH_RmlRenderForwarder final : public Rml::RenderInterface
{
  public:
    void SetActiveBackend(BH_RmlRenderBackend *backend)
    {
        backend_ = backend;
    }

    BH_RmlRenderBackend *GetActiveBackend() const
    {
        return backend_;
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        return backend_ ? backend_->CompileGeometry(vertices, indices) : 0;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        if (backend_)
            backend_->RenderGeometry(geometry, translation, texture);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        if (backend_)
            backend_->ReleaseGeometry(geometry);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions, const Rml::String &source) override
    {
        if (!backend_)
            return 0;

        Rml::TextureHandle handle = 0;
        bool success = backend_->LoadTexture(handle, texture_dimensions, source);
        return success ? handle : 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override
    {
        if (!backend_)
            return 0;

        Rml::TextureHandle handle = 0;
        bool success = backend_->GenerateTexture(handle, source.data(), source_dimensions);
        return success ? handle : 0;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        if (backend_)
            backend_->ReleaseTexture(texture);
    }

    void EnableScissorRegion(bool enable) override
    {
        if (backend_)
            backend_->EnableScissorRegion(enable);
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        if (backend_)
            backend_->SetScissorRegion(region);
    }

    void SetTransform(const Rml::Matrix4f *transform) override
    {
        if (backend_)
            backend_->SetTransform(transform);
    }

  private:
    BH_RmlRenderBackend *backend_ = nullptr;
};

// -----------------------------
//  Global RmlUi state
// -----------------------------

static void bh_rml_load_ttf_fonts_from_dir(const std::string &asset_root)
{
    namespace fs = std::filesystem;

    const fs::path font_dir = fs::path(asset_root) / "interface" / "fonts";
    if (!fs::exists(font_dir) || !fs::is_directory(font_dir))
    {
        SDL_Log("[bh][ui] Font directory not found: %s", font_dir.string().c_str());
        return;
    }

    int loaded = 0;
    try
    {
        for (const fs::directory_entry &ent : fs::directory_iterator(font_dir))
        {
            if (!ent.is_regular_file())
                continue;

            const fs::path p = ent.path();
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".ttf")
                continue;

            const std::string filename = p.filename().string();
            const std::string rml_path = std::string("interface/fonts/") + filename;

            if (Rml::LoadFontFace(rml_path))
            {
                loaded++;
            }
            else
            {
                SDL_Log("[bh][ui] Failed to load font: %s", rml_path.c_str());
            }
        }
    }
    catch (const std::exception &ex)
    {
        SDL_Log("[bh][ui] Font directory iteration failed: %s", ex.what());
    }

    if (loaded > 0)
    {
        SDL_Log("[bh][ui] Loaded %d font face(s) from interface/fonts", loaded);
    }
}

struct BH_RmlGlobal
{
    int refcount = 0;
    std::unique_ptr<BH_RmlSystemInterface> system_interface;
    std::unique_ptr<BH_RmlFileInterface> file_interface;
    std::unique_ptr<BH_RmlRenderForwarder> render_forwarder;
    std::string asset_root;

    void Acquire(const char *asset_root)
    {
        if (refcount == 0)
        {
            asset_root = asset_root ? asset_root : "";
            this->asset_root = asset_root;

            system_interface = std::make_unique<BH_RmlSystemInterface>();
            file_interface = std::make_unique<BH_RmlFileInterface>(this->asset_root);
            render_forwarder = std::make_unique<BH_RmlRenderForwarder>();

            Rml::SetSystemInterface(system_interface.get());
            Rml::SetFileInterface(file_interface.get());
            Rml::SetRenderInterface(render_forwarder.get());

            if (!Rml::Initialise())
            {
                SDL_Log("[bh][rml] Rml::Initialise() failed");
            }
            else
            {
                /* Fonts are global across contexts; load them once during global init. */
                bh_rml_load_ttf_fonts_from_dir(this->asset_root);
            }
        }

        refcount++;
    }

    void Release()
    {
        refcount--;
        if (refcount <= 0)
        {
            refcount = 0;
            Rml::Shutdown();
            render_forwarder.reset();
            file_interface.reset();
            system_interface.reset();
            asset_root.clear();
        }
    }
};

static BH_RmlGlobal g_rml;

} // namespace

// -----------------------------
//  BH_UI implementation (Global Scope)
// -----------------------------

struct BH_UI
{
    BH_Renderer *renderer = nullptr;
    SDL_Window *window = nullptr;

    std::string asset_root;

    std::unique_ptr<BH_RmlRenderBackend> backend;
    std::string context_name;
    Rml::Context *context = nullptr;

    /* Offscreen contexts owned by this BH_UI instance. */
    std::vector<std::unique_ptr<struct BH_UIContext>> offscreen_contexts;

    BH_RenderPassHooks hooks{};
};

struct BH_UIContext
{
    BH_UI *owner = nullptr;
    BH_Renderer *renderer = nullptr;

    std::unique_ptr<BH_RmlRenderBackend> backend;
    std::string context_name;
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;

    /* Optional owned render targets (color is also registered as a BH_TextureHandle). */
    BH_GPUTexture *rt_color = nullptr;
    BH_GPUTexture *rt_depth = nullptr;
    BH_TextureHandle rt_handle = 0;
    uint32_t rt_w = 0;
    uint32_t rt_h = 0;
};

// -----------------------------
//  Listeners Implementation (Namespace Context)
// -----------------------------

namespace
{

static void bh_ui_prepare_cb(void *user, BH_GPUCommandBuffer *cmd, BH_GPUCopyPass *copy_pass, const mat4 *view_proj,
                             uint32_t fb_w, uint32_t fb_h, float alpha)
{
    BH_UI *ui = reinterpret_cast<BH_UI *>(user);
    if (!ui || !ui->backend || !ui->context)
        return;

    (void)view_proj;
    (void)alpha;

    // 1. Activate backend for RenderInterface forwarding.
    g_rml.render_forwarder->SetActiveBackend(ui->backend.get());

    // 2. Set up the backend to handle uploads during the RmlUi Render traversal
    ui->backend->SetPreparePass(cmd, copy_pass, fb_w, fb_h);

    // 3. RmlUi generates geometry and triggers LoadTexture/GenerateTexture calls here.
    ui->context->Render();
}

static void bh_ui_draw_cb(void *user, BH_GPUCommandBuffer *cmd, BH_GPURenderPass *render_pass, const mat4 *view_proj,
                          uint32_t fb_w, uint32_t fb_h, float alpha)
{
    BH_UI *ui = reinterpret_cast<BH_UI *>(user);
    if (!ui || !ui->backend)
        return;

    (void)view_proj;
    (void)alpha;
    (void)fb_w;
    (void)fb_h;

    ui->backend->RenderToPass(cmd, render_pass);
}

static void bh_ui_end_frame_cb(void *user, bool submit_ok)
{
    BH_UI *ui = reinterpret_cast<BH_UI *>(user);
    if (!ui || !ui->backend)
        return;

    ui->backend->EndFrame(submit_ok);
}

static void bh_ui_context_release_render_target(BH_UIContext *ctx)
{
    if (!ctx || !ctx->renderer || !ctx->renderer->device)
    {
        return;
    }

    if (ctx->rt_handle != 0)
    {
        BH_TextureManager_ReleaseHandle(&ctx->renderer->textures, ctx->rt_handle);
        ctx->rt_handle = 0;
        ctx->rt_color = nullptr; /* released by texture manager */
    }
    else if (ctx->rt_color)
    {
        BH_GPU_ReleaseTexture(ctx->renderer->device, ctx->rt_color);
        ctx->rt_color = nullptr;
    }

    if (ctx->rt_depth)
    {
        BH_GPU_ReleaseTexture(ctx->renderer->device, ctx->rt_depth);
        ctx->rt_depth = nullptr;
    }

    ctx->rt_w = 0;
    ctx->rt_h = 0;
}

static void bh_ui_context_cleanup(BH_UIContext *ctx)
{
    if (!ctx)
    {
        return;
    }

    if (ctx->document)
    {
        ctx->document->Close();
        ctx->document = nullptr;
    }

    if (ctx->context)
    {
        Rml::RemoveContext(ctx->context_name);
        ctx->context = nullptr;
    }

    /* If this context owned the currently active backend, clear it to avoid a dangling pointer. */
    if (g_rml.render_forwarder && ctx->backend && g_rml.render_forwarder->GetActiveBackend() == ctx->backend.get())
    {
        g_rml.render_forwarder->SetActiveBackend(nullptr);
    }

    bh_ui_context_release_render_target(ctx);

    ctx->backend.reset();
    ctx->renderer = nullptr;
    ctx->owner = nullptr;
}

} // namespace

// -----------------------------
//  C API
// -----------------------------

extern "C"
{

    BH_UI *BH_UI_Create(BH_Renderer *renderer, SDL_Window *window, const char *asset_root)
    {
        if (!renderer || !window)
        {
            SDL_Log("[bh][ui] bh_ui_create: invalid args");
            return nullptr;
        }


        g_rml.Acquire(asset_root);

        std::unique_ptr<BH_UI> ui(new (std::nothrow) BH_UI());
        if (!ui)
        {
            SDL_Log("[bh][ui] Out of memory (BH_UI)");
            g_rml.Release();
            return nullptr;
        }
        ui->renderer = renderer;
        ui->window = window;
        ui->asset_root = asset_root ? asset_root : "";

        ui->backend.reset(new (std::nothrow) BH_RmlRenderBackend(renderer));
        if (!ui->backend)
        {
            SDL_Log("[bh][ui] Out of memory (BH_RmlRenderBackend)");
            g_rml.Release();
            return nullptr;
        }

        const Uint32 win_id = SDL_GetWindowID(window);
        ui->context_name = "bh_ui_" + std::to_string(win_id);

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        ui->context = Rml::CreateContext(ui->context_name, Rml::Vector2i(w, h));
        if (!ui->context)
        {
            SDL_Log("[bh][ui] Rml::CreateContext failed");
            g_rml.Release();
            return nullptr;
        }

        const float display_scale = SDL_GetWindowDisplayScale(window);
        if (display_scale > 0.f)
            ui->context->SetDensityIndependentPixelRatio(display_scale);

        SDL_Log("[bh][ui] Display Scale: %f", display_scale);

        /* Higher-level UI code may choose to enable/disable cursor handling.
           We enable it by default since most games want UI to control the cursor when visible. */
        ui->context->EnableMouseCursor(true);

        ui->hooks.prepare = bh_ui_prepare_cb;
        ui->hooks.draw = bh_ui_draw_cb;
        ui->hooks.end_frame = bh_ui_end_frame_cb;
        ui->hooks.user = ui.get();

        return ui.release();
    }

    void BH_UI_Destroy(BH_UI *ui)
    {
        if (!ui)
        {
            return;
        }

        /* Destroy any offscreen contexts owned by this UI instance before tearing down global Rml state. */
        for (auto &ctx : ui->offscreen_contexts)
        {
            bh_ui_context_cleanup(ctx.get());
        }
        ui->offscreen_contexts.clear();

        if (ui->context)
        {
            Rml::RemoveContext(ui->context_name);
            ui->context = nullptr;
        }

        /* If this UI owned the currently active backend, clear it to avoid a
           dangling pointer inside the RenderInterface forwarder. */
        if (g_rml.render_forwarder && g_rml.render_forwarder->GetActiveBackend() == ui->backend.get())
        {
            g_rml.render_forwarder->SetActiveBackend(nullptr);
        }

        ui->backend.reset();

        delete ui;

        g_rml.Release();
    }

    BH_RenderPassHooks BH_UI_GetRenderHooks(const BH_UI *ui)
    {
        if (!ui)
        {
            return BH_RenderPassHooks{nullptr, nullptr, nullptr, nullptr};
        }
        return ui->hooks;
    }

    SDL_Window *BH_UI_GetWindow(BH_UI *ui)
    {
        return ui ? ui->window : nullptr;
    }

    const char *BH_UI_GetAssetRoot(const BH_UI *ui)
    {
        return ui ? ui->asset_root.c_str() : "";
    }

    Rml::Context *BH_UI_GetRmlContext(BH_UI *ui)
    {
        return ui ? ui->context : nullptr;
    }

    BH_UIContext *BH_UIContext_Create(BH_UI *ui, const char *name, uint32_t width, uint32_t height)
    {
        if (!ui || !ui->renderer)
        {
            SDL_Log("[bh][ui] BH_UIContext_Create: ui/renderer is null");
            return nullptr;
        }
        if (width == 0 || height == 0)
        {
            SDL_Log("[bh][ui] BH_UIContext_Create: invalid size %ux%u", width, height);
            return nullptr;
        }

        std::unique_ptr<BH_UIContext> ctx(new (std::nothrow) BH_UIContext());
        if (!ctx)
        {
            SDL_Log("[bh][ui] Out of memory (BH_UIContext)");
            return nullptr;
        }

        ctx->owner = ui;
        ctx->renderer = ui->renderer;

        ctx->backend.reset(new (std::nothrow) BH_RmlRenderBackend(ui->renderer));
        if (!ctx->backend)
        {
            SDL_Log("[bh][ui] Out of memory (BH_RmlRenderBackend) for offscreen context");
            return nullptr;
        }

        static uint32_t s_id = 1;
        std::string base = (name && name[0]) ? name : "context";
        for (char &c : base)
        {
            if (!std::isalnum((unsigned char)c))
            {
                c = '_';
            }
        }

        ctx->context_name = "bh_ui_" + base + "_offscreen_" + std::to_string(s_id++);

        ctx->context = Rml::CreateContext(ctx->context_name, Rml::Vector2i((int)width, (int)height));
        if (!ctx->context)
        {
            SDL_Log("[bh][ui] Rml::CreateContext failed for offscreen context '%s'", ctx->context_name.c_str());
            bh_ui_context_cleanup(ctx.get());
            return nullptr;
        }

        /* Offscreen contexts should not influence OS cursor state by default. */
        ctx->context->EnableMouseCursor(false);

        ui->offscreen_contexts.push_back(std::move(ctx));
        return ui->offscreen_contexts.back().get();
    }

    void BH_UIContext_Destroy(BH_UI *ui, BH_UIContext *ctx)
    {
        if (!ui || !ctx)
        {
            return;
        }

        auto &vec = ui->offscreen_contexts;
        for (auto it = vec.begin(); it != vec.end(); ++it)
        {
            if (it->get() == ctx)
            {
                bh_ui_context_cleanup(ctx);
                vec.erase(it);
                return;
            }
        }

        SDL_Log("[bh][ui] BH_UIContext_Destroy: context not owned by this BH_UI instance");
    }

    bool BH_UIContext_SetRenderTarget(BH_UIContext *ctx, uint32_t width, uint32_t height)
    {
        if (!ctx || !ctx->renderer || !ctx->renderer->device || !ctx->context)
        {
            return false;
        }
        if (width == 0 || height == 0)
        {
            return false;
        }

        bh_ui_context_release_render_target(ctx);

        BH_GPUTextureCreateInfo color_ci{};
        color_ci.type = BH_GPU_TEXTURETYPE_2D;
        color_ci.format = ctx->renderer->swapchain_format;
        color_ci.width = width;
        color_ci.height = height;
        color_ci.layer_count_or_depth = 1;
        color_ci.num_levels = 1;
        color_ci.sample_count = BH_GPU_SAMPLECOUNT_1;
        color_ci.usage = BH_GPU_TEXTUREUSAGE_COLOR_TARGET | BH_GPU_TEXTUREUSAGE_SAMPLER;

        BH_GPUTexture *color_tex = BH_GPU_CreateTexture(ctx->renderer->device, &color_ci);
        if (!color_tex)
        {
            SDL_Log("[bh][ui] BH_GPU_CreateTexture (ui rt color) failed: %s", BH_GPU_GetLastError());
            return false;
        }

        BH_GPUTextureCreateInfo depth_ci{};
        depth_ci.type = BH_GPU_TEXTURETYPE_2D;
        depth_ci.format = ctx->renderer->depth_format;
        depth_ci.width = width;
        depth_ci.height = height;
        depth_ci.layer_count_or_depth = 1;
        depth_ci.num_levels = 1;
        depth_ci.sample_count = BH_GPU_SAMPLECOUNT_1;
        depth_ci.usage = BH_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

        BH_GPUTexture *depth_tex = BH_GPU_CreateTexture(ctx->renderer->device, &depth_ci);
        if (!depth_tex)
        {
            SDL_Log("[bh][ui] BH_GPU_CreateTexture (ui rt depth) failed: %s", BH_GPU_GetLastError());
            BH_GPU_ReleaseTexture(ctx->renderer->device, color_tex);
            return false;
        }

        const std::string dbg_name = "__ui_rt_" + ctx->context_name;
        const BH_TextureHandle th = BH_TextureManager_RegisterExternalTexture(
            &ctx->renderer->textures, dbg_name.c_str(), color_tex, width, height, false, true);

        if (th == 0)
        {
            SDL_Log("[bh][ui] BH_TextureManager_RegisterExternalTexture failed for '%s'", dbg_name.c_str());
            BH_GPU_ReleaseTexture(ctx->renderer->device, depth_tex);
            BH_GPU_ReleaseTexture(ctx->renderer->device, color_tex);
            return false;
        }

        ctx->rt_color = color_tex;
        ctx->rt_depth = depth_tex;
        ctx->rt_handle = th;
        ctx->rt_w = width;
        ctx->rt_h = height;

        ctx->context->SetDimensions(Rml::Vector2i((int)width, (int)height));

        return true;
    }


    BH_TextureHandle BH_UIContext_GetRenderTargetHandle(const BH_UIContext *ctx)
    {
        return ctx ? ctx->rt_handle : 0;
    }

    BH_GPUTexture *BH_UIContext_GetRenderTargetTexture(const BH_UIContext *ctx)
    {
        return ctx ? ctx->rt_color : nullptr;
    }

    void BH_UIContext_SetElementInnerRML(BH_UIContext *ctx, const char *element_id, const char *rml)
    {
        if (!ctx || !ctx->context || !ctx->document || !element_id || !rml)
        {
            return;
        }
        Rml::Element *elem = ctx->document->GetElementById(element_id);
        if (!elem)
        {
            SDL_Log("[bh][ui] SetElementInnerRML: element '%s' not found in context '%s'", element_id,
                    ctx->context_name.c_str());
            return;
        }
        elem->SetInnerRML(rml);
    }

    bool BH_UIContext_LoadDocumentFromMemory(BH_UIContext *ctx, const char *rml)
    {
        if (!ctx || !ctx->context || !rml)
        {
            return false;
        }

        if (ctx->document)
        {
            ctx->document->Close();
            ctx->document = nullptr;
        }

        ctx->document = ctx->context->LoadDocumentFromMemory(rml, ctx->context_name);
        if (!ctx->document)
        {
            SDL_Log("[bh][ui] LoadDocumentFromMemory failed for context '%s'", ctx->context_name.c_str());
            return false;
        }

        ctx->document->Show();
        return true;
    }

    bool BH_UIContext_Render(BH_UIContext *ctx)
    {
        if (!ctx || !ctx->renderer || !ctx->renderer->device || !ctx->backend || !ctx->context)
        {
            return false;
        }
        if (!ctx->rt_color || !ctx->rt_depth || ctx->rt_w == 0 || ctx->rt_h == 0)
        {
            return false;
        }

        /* Drive layout/animations. */
        ctx->context->Update();

        BH_GPUDevice *device = ctx->renderer->device;

        BH_GPUCommandBuffer *cmd = BH_GPU_AcquireCommandBuffer(device);
        if (!cmd)
        {
            SDL_Log("[bh][ui] BH_GPU_AcquireCommandBuffer failed: %s", BH_GPU_GetLastError());
            return false;
        }

        BH_GPUCopyPass *copy_pass = BH_GPU_BeginCopyPass(cmd);
        if (!copy_pass)
        {
            SDL_Log("[bh][ui] BH_GPU_BeginCopyPass failed: %s", BH_GPU_GetLastError());
            BH_GPU_CancelCommandBuffer(cmd);
            ctx->backend->EndFrame(false);
            return false;
        }

        /* Activate backend for RenderInterface forwarding and allow uploads during Render(). */
        g_rml.render_forwarder->SetActiveBackend(ctx->backend.get());
        ctx->backend->SetPreparePass(cmd, copy_pass, ctx->rt_w, ctx->rt_h);

        /* Generates draw commands + may enqueue texture uploads. */
        ctx->context->Render();

        BH_GPU_EndCopyPass(copy_pass);

        BH_GPUColorTargetInfo color_ti{};
        color_ti.texture = ctx->rt_color;
        color_ti.load_op = BH_GPU_LOADOP_CLEAR;
        color_ti.store_op = BH_GPU_STOREOP_STORE;
        color_ti.clear_color = BH_GPUColor{0.f, 0.f, 0.f, 0.f};

        BH_GPUDepthStencilTargetInfo depth_ti{};
        depth_ti.texture = ctx->rt_depth;
        depth_ti.load_op = BH_GPU_LOADOP_CLEAR;
        depth_ti.store_op = BH_GPU_STOREOP_DONT_CARE;
        depth_ti.clear_depth = 1.0f;

        BH_GPURenderPass *pass = BH_GPU_BeginRenderPass(cmd, &color_ti, 1, &depth_ti);
        if (!pass)
        {
            SDL_Log("[bh][ui] BH_GPU_BeginRenderPass (offscreen) failed: %s", BH_GPU_GetLastError());
            BH_GPU_CancelCommandBuffer(cmd);
            ctx->backend->EndFrame(false);
            g_rml.render_forwarder->SetActiveBackend(nullptr);
            return false;
        }

        ctx->backend->RenderToPass(cmd, pass);

        BH_GPU_EndRenderPass(pass);

        const bool ok = BH_GPU_SubmitCommandBuffer(cmd);
        ctx->backend->EndFrame(ok);

        /* Avoid leaving a dangling active backend when multiple contexts render. */
        g_rml.render_forwarder->SetActiveBackend(nullptr);

        return ok;
    }
}