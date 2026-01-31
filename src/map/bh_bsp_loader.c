/* -----------------------------------------------------------------------------
   bh_bsp_loader.c
   ----------------------------------------------------------------------------- */

#include "bh_bsp_loader.h"

#include "../core/bh_file.h"
#include "../entity/types/bh_player_entity.h"
#include "../entity/types/bh_static_geometry_entity.h"
#include "../physics/bh_physics.h"
#include "../render/bh_material.h"
#include "../render/bh_material_manager.h"
#include "../render/bh_renderer.h"
#include "../render/bh_texture_manager.h"
#include "../scene/bh_scene.h"

#include <SDL3/SDL.h>
#include <math.h>

/* -----------------------------------------------------------------------------
   VBSP structs
   ----------------------------------------------------------------------------- */

#pragma pack(push, 1)

typedef struct BH_VBSP_Lump
{
    int32_t fileofs;
    int32_t filelen;
    int32_t version;
    char fourCC[4];
} BH_VBSP_Lump;

typedef struct BH_VBSP_Header
{
    char ident[4];
    int32_t version;
    BH_VBSP_Lump lumps[64];
    int32_t mapRevision;
} BH_VBSP_Header;

typedef struct BH_VBSP_Plane
{
    float normal[3];
    float dist;
    int32_t type;
} BH_VBSP_Plane;

typedef struct BH_VBSP_Vertex
{
    float position[3];
} BH_VBSP_Vertex;

typedef struct BH_VBSP_Edge
{
    uint16_t v[2];
} BH_VBSP_Edge;

typedef struct BH_VBSP_Face
{
    uint16_t planeNum;
    uint8_t planeSide;
    uint8_t onNode;

    int32_t firstEdge;
    int16_t numEdges;
    int16_t texInfo;

    int16_t dispInfo;
    int16_t surfaceFogVolumeID;

    uint8_t styles[4];
    int32_t lightOfs;

    float area;

    int32_t lightmapTextureMinsInLuxels[2];
    int32_t lightmapTextureSizeInLuxels[2];

    int32_t origFace;
    uint16_t numPrims;
    uint16_t firstPrimID;
    uint32_t smoothingGroups;
} BH_VBSP_Face;

typedef struct BH_VBSP_TexInfo
{
    float textureVecsS[4];
    float textureVecsT[4];

    float lightmapVecsS[4];
    float lightmapVecsT[4];

    int32_t flags;
    int32_t texData;
} BH_VBSP_TexInfo;

typedef struct BH_VBSP_TexData
{
    float reflectivity[3];
    int32_t nameStringTableID;

    int32_t width;
    int32_t height;

    int32_t view_width;
    int32_t view_height;
} BH_VBSP_TexData;

typedef struct BH_VBSP_Brush
{
    int32_t firstSide;
    int32_t numSides;
    int32_t contents;
} BH_VBSP_Brush;

typedef struct BH_VBSP_BrushSide
{
    uint16_t planeNum;
    int16_t texInfo;
    int16_t dispInfo;
    int16_t bevel;
} BH_VBSP_BrushSide;

#pragma pack(pop)

enum
{
    BH_LUMP_ENTITIES = 0,
    BH_LUMP_PLANES = 1,
    BH_LUMP_TEXDATA = 2,
    BH_LUMP_VERTEXES = 3,
    BH_LUMP_TEXINFO = 6,
    BH_LUMP_FACES = 7,
    BH_LUMP_LIGHTING = 8,
    BH_LUMP_EDGES = 12,
    BH_LUMP_SURFEDGES = 13,
    BH_LUMP_BRUSHES = 18,
    BH_LUMP_BRUSHSIDES = 19,
    BH_LUMP_TEXDATA_STRING_DATA = 43,
    BH_LUMP_TEXDATA_STRING_TABLE = 44,
};

/* -----------------------------------------------------------------------------
   Helpers
   ----------------------------------------------------------------------------- */

typedef struct BH_BspLightmapRect
{
    uint16_t x, y, w, h;
    bool allocated;
} BH_BspLightmapRect;

typedef struct BH_BspLightmapPackItem
{
    int32_t face_index;
    int32_t width;  /* including border (+3) */
    int32_t height; /* including border (+3) */
    int32_t area;
} BH_BspLightmapPackItem;

static BH_SceneNode *bh_create_named_child(BH_Scene *scene, BH_SceneNode *parent, BH_Arena *arena, const char *name)
{
    BH_SceneNode *n = BH_Scene_CreateNode(arena, name);
    if (n)
    {
        BH_Transform t = BH_Transform_GetIdentity();
        BH_Scene_SetNodeLocalTransform(n, t, t);
        BH_Scene_AddChild(parent ? parent : scene->root, n);
    }
    return n;
}

static bool bh_vbsp_get_lump(const uint8_t *file, size_t file_size, const BH_VBSP_Header *hdr, int lump_idx,
                            const void **out_ptr, uint32_t *out_len)
{
    if (!file || !hdr || lump_idx < 0 || lump_idx >= 64 || !out_ptr || !out_len)
    {
        return false;
    }

    const BH_VBSP_Lump *l = &hdr->lumps[lump_idx];
    if (l->fileofs < 0 || l->filelen < 0)
    {
        return false;
    }

    const size_t ofs = (size_t)l->fileofs;
    const size_t len = (size_t)l->filelen;
    if (ofs + len > file_size)
    {
        return false;
    }

    *out_ptr = file + ofs;
    *out_len = (uint32_t)len;
    return true;
}

static uint32_t bh_fnv1a_u32(const char *s)
{
    if (!s)
        return 0u;

    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        h ^= (uint32_t)(*p);
        h *= 16777619u;
    }
    return h;
}

static vec3 bh_tint_from_name(const char *name)
{
    /* Subtle, stable tint per texture name. */
    const uint32_t h = bh_fnv1a_u32(name ? name : "");

    const float r = 0.65f + 0.30f * ((float)((h >> 0) & 0xFFu) / 255.0f);
    const float g = 0.65f + 0.30f * ((float)((h >> 8) & 0xFFu) / 255.0f);
    const float b = 0.65f + 0.30f * ((float)((h >> 16) & 0xFFu) / 255.0f);

    return (vec3){r, g, b};
}

static uint8_t bh_u8_from_f01(float x)
{
    if (x <= 0.0f)
        return 0u;
    if (x >= 1.0f)
        return 255u;
    const int v = (int)lrintf(x * 255.0f);
    return (uint8_t)((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

static void bh_encode_rgbm(vec3 lin, uint8_t out_rgba[4])
{
    /* Clamp negative */
    if (lin.x < 0.0f)
        lin.x = 0.0f;
    if (lin.y < 0.0f)
        lin.y = 0.0f;
    if (lin.z < 0.0f)
        lin.z = 0.0f;

    float maxc = lin.x;
    if (lin.y > maxc)
        maxc = lin.y;
    if (lin.z > maxc)
        maxc = lin.z;

    float mult = maxc;
    if (mult < 1.0f)
        mult = 1.0f;
    if (mult > 16.0f)
        mult = 16.0f;

    const float inv_mult = 1.0f / mult;

    const float inv_gamma = 1.0f / 2.2f;
    const float r = powf(lin.x * inv_mult, inv_gamma);
    const float g = powf(lin.y * inv_mult, inv_gamma);
    const float b = powf(lin.z * inv_mult, inv_gamma);

    const float a = (mult - 1.0f) / 15.0f;

    out_rgba[0] = bh_u8_from_f01(r);
    out_rgba[1] = bh_u8_from_f01(g);
    out_rgba[2] = bh_u8_from_f01(b);
    out_rgba[3] = bh_u8_from_f01(a);
}

static vec3 bh_decode_rgbexp32(uint8_t r, uint8_t g, uint8_t b, int8_t exp)
{
    const float inv255 = 1.0f / 255.0f;
    const float scale = powf(2.0f, (float)exp);
    return (vec3){(float)r * inv255 * scale, (float)g * inv255 * scale, (float)b * inv255 * scale};
}

static int bh_next_pow2(int x)
{
    if (x < 1)
        return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

static int bh_pack_item_cmp_desc(const void *a, const void *b)
{
    const BH_BspLightmapPackItem *ia = (const BH_BspLightmapPackItem *)a;
    const BH_BspLightmapPackItem *ib = (const BH_BspLightmapPackItem *)b;

    if (ia->area != ib->area)
        return (ib->area - ia->area);
    if (ia->width != ib->width)
        return (ib->width - ia->width);
    return (ib->height - ia->height);
}

static bool bh_try_pack_rectangles(int atlas_w, int atlas_h, const BH_BspLightmapPackItem *items, int item_count,
                                   BH_BspLightmapRect *regions, int region_count)
{
    int current_x = 0;
    int current_y = 0;
    int row_h = 0;

    for (int i = 0; i < item_count; ++i)
    {
        const BH_BspLightmapPackItem *it = &items[i];
        if (it->width <= 0 || it->height <= 0)
            continue;

        if (it->width > atlas_w || it->height > atlas_h)
            return false;

        if (current_x + it->width > atlas_w)
        {
            current_y += row_h;
            current_x = 0;
            row_h = 0;
        }

        if (current_y + it->height > atlas_h)
            return false;

        const int data_x = current_x + 1;
        const int data_y = current_y + 1;
        const int data_w = it->width - 2;
        const int data_h = it->height - 2;

        if (it->face_index >= 0 && it->face_index < region_count)
        {
            regions[it->face_index] = (BH_BspLightmapRect){
                .x = (uint16_t)data_x,
                .y = (uint16_t)data_y,
                .w = (uint16_t)((data_w < 0) ? 0 : data_w),
                .h = (uint16_t)((data_h < 0) ? 0 : data_h),
                .allocated = true,
            };
        }

        current_x += it->width;
        if (it->height > row_h)
            row_h = it->height;
    }

    return true;
}

static const char *bh_texdata_name(const BH_VBSP_TexData *texdata, int texdata_count, const int32_t *string_table,
                                  int string_table_count, const char *string_data, int string_data_len, int texdata_idx)
{
    if (!texdata || texdata_idx < 0 || texdata_idx >= texdata_count)
        return "";

    const int32_t st_id = texdata[texdata_idx].nameStringTableID;
    if (!string_table || st_id < 0 || st_id >= string_table_count)
        return "";

    const int32_t ofs = string_table[st_id];
    if (!string_data || ofs < 0 || ofs >= string_data_len)
        return "";

    return string_data + ofs;
}

static bool bh_parse_vec3_str(const char *s, vec3 *out_v)
{
    if (!s || !out_v)
        return false;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (SDL_sscanf(s, "%f %f %f", &x, &y, &z) != 3)
        return false;

    *out_v = (vec3){x, y, z};
    return true;
}

static bool bh_parse_entities_for_spawn(const char *ents, vec3 *out_origin, vec3 *out_angles)
{
    if (!ents || !out_origin || !out_angles)
        return false;

    const char *p = ents;
    while (*p)
    {
        /* Find next entity block */
        while (*p && *p != '{')
            ++p;
        if (!*p)
            break;
        ++p; /* skip '{' */

        const char *classname = NULL;
        const char *origin = NULL;
        const char *angles = NULL;
        const char *angle = NULL;

        while (*p)
        {
            while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
                ++p;
            if (*p == '}')
            {
                ++p;
                break;
            }
            if (*p != '"')
            {
                ++p;
                continue;
            }

            /* key */
            ++p;
            const char *k = p;
            while (*p && *p != '"')
                ++p;
            if (!*p)
                break;
            const int klen = (int)(p - k);
            ++p; /* closing quote */

            while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
                ++p;
            if (*p != '"')
                continue;

            /* value */
            ++p;
            const char *v = p;
            while (*p && *p != '"')
                ++p;
            if (!*p)
                break;
            const int vlen = (int)(p - v);
            ++p;

            if (klen == 9 && SDL_strncmp(k, "classname", 9) == 0)
                classname = v;
            else if (klen == 6 && SDL_strncmp(k, "origin", 6) == 0)
                origin = v;
            else if (klen == 6 && SDL_strncmp(k, "angles", 6) == 0)
                angles = v;
            else if (klen == 5 && SDL_strncmp(k, "angle", 5) == 0)
                angle = v;

            (void)vlen;
        }

        if (classname)
        {
            const bool is_start = (SDL_strncmp(classname, "info_player_start", 17) == 0);
            const bool is_dm = (SDL_strncmp(classname, "info_player_deathmatch", 22) == 0);
            if (is_start || is_dm)
            {
                vec3 org = {0, 0, 96};
                vec3 ang = {0, 0, 0};

                if (origin)
                    (void)bh_parse_vec3_str(origin, &org);

                if (angles)
                    (void)bh_parse_vec3_str(angles, &ang);
                else if (angle)
                {
                    float yaw = 0.0f;
                    if (SDL_sscanf(angle, "%f", &yaw) == 1)
                        ang = (vec3){0.0f, yaw, 0.0f};
                }

                *out_origin = org;
                *out_angles = ang;
                return true;
            }
        }
    }

    return false;
}

static vec2 bh_calc_uv(vec3 world_pos, const BH_VBSP_TexInfo *ti, const BH_VBSP_TexData *td)
{
    if (!ti || !td)
        return (vec2){0, 0};

    const vec3 s = vec3_make(ti->textureVecsS[0], ti->textureVecsS[1], ti->textureVecsS[2]);
    const vec3 t = vec3_make(ti->textureVecsT[0], ti->textureVecsT[1], ti->textureVecsT[2]);

    const float so = ti->textureVecsS[3];
    const float to = ti->textureVecsT[3];

    float u = vec3_dot(world_pos, s) + so;
    float v = vec3_dot(world_pos, t) + to;

    const float w = (td->width > 0) ? (float)td->width : 1.0f;
    const float h = (td->height > 0) ? (float)td->height : 1.0f;

    u /= w;
    v /= h;

    return (vec2){u, v};
}

static vec2 bh_calc_lightmap_uv2(vec3 world_pos, const BH_VBSP_TexInfo *ti, const BH_VBSP_Face *face,
                                 int face_index, const BH_BspLightmapRect *regions, int region_count,
                                 int atlas_w, int atlas_h)
{
    if (!ti || !face || !regions || face_index < 0 || face_index >= region_count || atlas_w <= 0 || atlas_h <= 0)
        return (vec2){0.5f, 0.5f};

    const BH_BspLightmapRect r = regions[face_index];
    if (!r.allocated || face->lightOfs < 0)
        return (vec2){0.5f, 0.5f};

    const float size_x = (float)face->lightmapTextureSizeInLuxels[0];
    const float size_y = (float)face->lightmapTextureSizeInLuxels[1];
    if (size_x <= 0.0f || size_y <= 0.0f)
        return (vec2){0.5f, 0.5f};

    const vec3 s = vec3_make(ti->lightmapVecsS[0], ti->lightmapVecsS[1], ti->lightmapVecsS[2]);
    const vec3 t = vec3_make(ti->lightmapVecsT[0], ti->lightmapVecsT[1], ti->lightmapVecsT[2]);

    const float so = ti->lightmapVecsS[3];
    const float to = ti->lightmapVecsT[3];

    float luxel_s = vec3_dot(world_pos, s) + so;
    float luxel_t = vec3_dot(world_pos, t) + to;

    luxel_s -= (float)face->lightmapTextureMinsInLuxels[0];
    luxel_t -= (float)face->lightmapTextureMinsInLuxels[1];

    luxel_s = (luxel_s + 0.5f) / size_x;
    luxel_t = (luxel_t + 0.5f) / size_y;

    if (luxel_s < 0.0f)
        luxel_s = 0.0f;
    if (luxel_s > 1.0f)
        luxel_s = 1.0f;
    if (luxel_t < 0.0f)
        luxel_t = 0.0f;
    if (luxel_t > 1.0f)
        luxel_t = 1.0f;

    const float inv_w = 1.0f / (float)atlas_w;
    const float inv_h = 1.0f / (float)atlas_h;

    const float atlas_min_u = ((float)r.x + 0.5f) * inv_w;
    const float atlas_min_v_top = ((float)r.y + 0.5f) * inv_h;

    const float eff_w = ((r.w > 0) ? (float)r.w : 0.0f) - 1.0f;
    const float eff_h = ((r.h > 0) ? (float)r.h : 0.0f) - 1.0f;

    const float atlas_size_u = ((eff_w > 0.0f) ? eff_w : 0.0f) * inv_w;
    const float atlas_size_v = ((eff_h > 0.0f) ? eff_h : 0.0f) * inv_h;

    const float u = atlas_min_u + luxel_s * atlas_size_u;
    const float v_top = atlas_min_v_top + luxel_t * atlas_size_v;

    /* Our shaders flip uv2.y before sampling, so provide bottom-left style coords here. */
    return (vec2){u, 1.0f - v_top};
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Bsp_LoadIntoScene(const char *bsp_abs_path, BH_Renderer *renderer, BH_Scene *scene, BH_PhysicsWorld *physics,
                          BH_EntityServices *sv, BH_Arena *level_arena, BH_BspLoadResult *out_result)
{
    if (out_result)
        *out_result = (BH_BspLoadResult){0};

    if (!bsp_abs_path || !renderer || !renderer->device || !scene || !scene->root || !physics || !sv || !level_arena)
    {
        return false;
    }

    BH_FileData fd = {0};
    if (!BH_File_ReadAll(bsp_abs_path, &fd))
    {
        SDL_Log("[bh] bsp: failed to read %s", bsp_abs_path);
        return false;
    }

    const uint8_t *file = (const uint8_t *)fd.data;
    const size_t file_size = fd.size;

    if (!file || file_size < sizeof(BH_VBSP_Header))
    {
        BH_File_Free(&fd);
        return false;
    }

    const BH_VBSP_Header *hdr = (const BH_VBSP_Header *)file;
    if (SDL_memcmp(hdr->ident, "VBSP", 4) != 0)
    {
        SDL_Log("[bh] bsp: invalid ident (expected VBSP)");
        BH_File_Free(&fd);
        return false;
    }

    /* ---------------------------------------------------------------------
       Gather lumps
       --------------------------------------------------------------------- */

    const void *p_planes = NULL, *p_verts = NULL, *p_edges = NULL, *p_surfedges = NULL, *p_faces = NULL,
               *p_texinfo = NULL, *p_texdata = NULL, *p_lighting = NULL, *p_brushes = NULL, *p_brushsides = NULL,
               *p_strdata = NULL, *p_strtable = NULL, *p_entities = NULL;

    uint32_t len_planes = 0, len_verts = 0, len_edges = 0, len_surfedges = 0, len_faces = 0, len_texinfo = 0,
             len_texdata = 0, len_lighting = 0, len_brushes = 0, len_brushsides = 0, len_strdata = 0, len_strtable = 0,
             len_entities = 0;

    (void)bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_ENTITIES, &p_entities, &len_entities);

    if (!bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_PLANES, &p_planes, &len_planes) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_VERTEXES, &p_verts, &len_verts) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_EDGES, &p_edges, &len_edges) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_SURFEDGES, &p_surfedges, &len_surfedges) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_FACES, &p_faces, &len_faces) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_TEXINFO, &p_texinfo, &len_texinfo) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_TEXDATA, &p_texdata, &len_texdata) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_LIGHTING, &p_lighting, &len_lighting) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_BRUSHES, &p_brushes, &len_brushes) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_BRUSHSIDES, &p_brushsides, &len_brushsides) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_TEXDATA_STRING_DATA, &p_strdata, &len_strdata) ||
        !bh_vbsp_get_lump(file, file_size, hdr, BH_LUMP_TEXDATA_STRING_TABLE, &p_strtable, &len_strtable))
    {
        SDL_Log("[bh] bsp: missing required lumps");
        BH_File_Free(&fd);
        return false;
    }

    const BH_VBSP_Plane *planes = (const BH_VBSP_Plane *)p_planes;
    const BH_VBSP_Vertex *verts = (const BH_VBSP_Vertex *)p_verts;
    const BH_VBSP_Edge *edges = (const BH_VBSP_Edge *)p_edges;
    const int32_t *surfedges = (const int32_t *)p_surfedges;
    const BH_VBSP_Face *faces = (const BH_VBSP_Face *)p_faces;
    const BH_VBSP_TexInfo *texinfo = (const BH_VBSP_TexInfo *)p_texinfo;
    const BH_VBSP_TexData *texdata = (const BH_VBSP_TexData *)p_texdata;
    const uint8_t *lighting = (const uint8_t *)p_lighting;
    const BH_VBSP_Brush *brushes = (const BH_VBSP_Brush *)p_brushes;
    const BH_VBSP_BrushSide *brushsides = (const BH_VBSP_BrushSide *)p_brushsides;
    const char *strdata = (const char *)p_strdata;
    const int32_t *strtable = (const int32_t *)p_strtable;

    const int plane_count = (int)(len_planes / sizeof(BH_VBSP_Plane));
    const int vert_count = (int)(len_verts / sizeof(BH_VBSP_Vertex));
    const int edge_count = (int)(len_edges / sizeof(BH_VBSP_Edge));
    const int surfedge_count = (int)(len_surfedges / sizeof(int32_t));
    const int face_count = (int)(len_faces / sizeof(BH_VBSP_Face));
    const int texinfo_count = (int)(len_texinfo / sizeof(BH_VBSP_TexInfo));
    const int texdata_count = (int)(len_texdata / sizeof(BH_VBSP_TexData));
    const int brush_count = (int)(len_brushes / sizeof(BH_VBSP_Brush));
    const int brushside_count = (int)(len_brushsides / sizeof(BH_VBSP_BrushSide));
    const int strtable_count = (int)(len_strtable / sizeof(int32_t));

    if (plane_count <= 0 || vert_count <= 0 || edge_count <= 0 || surfedge_count <= 0 || face_count <= 0 ||
        texinfo_count <= 0 || texdata_count <= 0)
    {
        SDL_Log("[bh] bsp: invalid lump sizes");
        BH_File_Free(&fd);
        return false;
    }

    /* ---------------------------------------------------------------------
       Spawn (optional)
       --------------------------------------------------------------------- */

    if (p_entities && len_entities > 0 && out_result)
    {
        char *ents = (char *)SDL_malloc((size_t)len_entities + 1u);
        if (ents)
        {
            SDL_memcpy(ents, p_entities, len_entities);
            ents[len_entities] = '\0';

            vec3 org = {0, 0, 96};
            vec3 ang = {0, 0, 0};

            if (bh_parse_entities_for_spawn(ents, &org, &ang))
            {
                BH_SceneNode *player_node = BH_Scene_CreateNode(level_arena, "bh_player");
                if (player_node)
                {
                    BH_Transform t = BH_Transform_GetIdentity();
                    t.position = org;
                    BH_Scene_SetNodeLocalTransform(player_node, t, t);
                    BH_Scene_AddChild(scene->root, player_node);

                    BH_PlayerEntity *e = bh_player_entity_create(level_arena);
                    if (e)
                    {
                        BH_Entity_Attach(scene, (BH_Entity *)e, player_node);
                        out_result->player_node = player_node;
                    }
                }

                (void)ang; /* view orientation currently handled elsewhere */
            }

            SDL_free(ents);
        }
    }

    /* ---------------------------------------------------------------------
       Build lightmap atlas layout
       --------------------------------------------------------------------- */

    BH_BspLightmapRect *lm_regions = (BH_BspLightmapRect *)BH_Arena_Alloc(level_arena, (size_t)face_count * sizeof(*lm_regions), 8);
    if (!lm_regions)
    {
        BH_File_Free(&fd);
        return false;
    }
    SDL_memset(lm_regions, 0, (size_t)face_count * sizeof(*lm_regions));

    BH_BspLightmapPackItem *items = (BH_BspLightmapPackItem *)SDL_malloc((size_t)face_count * sizeof(*items));
    int item_count = 0;
    int max_item_w = 0;
    int max_item_h = 0;

    if (items)
    {
        for (int fi = 0; fi < face_count; ++fi)
        {
            const BH_VBSP_Face *f = &faces[fi];
            if (f->lightOfs < 0)
                continue;

            const int sx = f->lightmapTextureSizeInLuxels[0];
            const int sy = f->lightmapTextureSizeInLuxels[1];
            if (sx < 0 || sy < 0)
                continue;

            const int w = sx + 3;
            const int h = sy + 3;
            if (w <= 0 || h <= 0)
                continue;

            items[item_count++] = (BH_BspLightmapPackItem){
                .face_index = fi,
                .width = w,
                .height = h,
                .area = w * h,
            };

            if (w > max_item_w)
                max_item_w = w;
            if (h > max_item_h)
                max_item_h = h;
        }

        if (item_count > 1)
        {
            qsort(items, (size_t)item_count, sizeof(*items), bh_pack_item_cmp_desc);
        }
    }

    int atlas_w = 0;
    int atlas_h = 0;
    bool lm_layout_ok = false;

    if (item_count > 0)
    {
        atlas_w = bh_next_pow2(max_item_w);
        atlas_h = bh_next_pow2(max_item_h);

        const int MAX_DIM = 8192;
        while (atlas_w <= MAX_DIM && atlas_h <= MAX_DIM)
        {
            SDL_memset(lm_regions, 0, (size_t)face_count * sizeof(*lm_regions));

            if (bh_try_pack_rectangles(atlas_w, atlas_h, items, item_count, lm_regions, face_count))
            {
                lm_layout_ok = true;
                break;
            }

            if (atlas_w >= MAX_DIM && atlas_h >= MAX_DIM)
                break;

            if (atlas_h <= atlas_w)
                atlas_h = (atlas_h < MAX_DIM) ? (atlas_h << 1) : MAX_DIM;
            else
                atlas_w = (atlas_w < MAX_DIM) ? (atlas_w << 1) : MAX_DIM;

            if (atlas_w >= MAX_DIM && atlas_h >= MAX_DIM)
                break;
        }
    }

    if (items)
        SDL_free(items);

    /* ---------------------------------------------------------------------
       Build lightmap atlas pixels (RGBA8 RGBM)
       --------------------------------------------------------------------- */

    BH_TextureHandle lightmap_atlas_handle = 0;

    if (lm_layout_ok && atlas_w > 0 && atlas_h > 0)
    {
        const size_t pixel_count = (size_t)atlas_w * (size_t)atlas_h;
        uint8_t *atlas_rgba = (uint8_t *)SDL_malloc(pixel_count * 4u);

        if (atlas_rgba)
        {
            /* Neutral 1.0 in RGBM => rgb=(1,1,1), a=0 */
            for (size_t i = 0; i < pixel_count; ++i)
            {
                atlas_rgba[i * 4 + 0] = 255;
                atlas_rgba[i * 4 + 1] = 255;
                atlas_rgba[i * 4 + 2] = 255;
                atlas_rgba[i * 4 + 3] = 0;
            }

            for (int fi = 0; fi < face_count; ++fi)
            {
                const BH_VBSP_Face *f = &faces[fi];
                const BH_BspLightmapRect r = lm_regions[fi];
                if (!r.allocated)
                    continue;
                if (f->lightOfs < 0)
                    continue;

                const int sx = f->lightmapTextureSizeInLuxels[0];
                const int sy = f->lightmapTextureSizeInLuxels[1];
                if (sx < 0 || sy < 0)
                    continue;

                const int face_w = sx + 1;
                const int face_h = sy + 1;

                int style_count = 0;
                for (int s = 0; s < 4; ++s)
                {
                    if (f->styles[s] != 255)
                        style_count++;
                }
                if (style_count <= 0)
                    style_count = 1;

                const int bytes_per_luxel = 4;
                const int row_stride = face_w * style_count * bytes_per_luxel;

                const int32_t light_ofs = f->lightOfs;
                const int64_t needed = (int64_t)light_ofs + (int64_t)face_h * (int64_t)row_stride;
                if (light_ofs < 0 || needed > (int64_t)len_lighting)
                {
                    continue;
                }

                for (int ly = 0; ly < face_h; ++ly)
                {
                    for (int lx = 0; lx < face_w; ++lx)
                    {
                        const int src_ofs = light_ofs + (ly * row_stride) + (lx * style_count * bytes_per_luxel);
                        const uint8_t rr = lighting[src_ofs + 0];
                        const uint8_t gg = lighting[src_ofs + 1];
                        const uint8_t bb = lighting[src_ofs + 2];
                        const int8_t ee = (int8_t)lighting[src_ofs + 3];

                        const vec3 lin = bh_decode_rgbexp32(rr, gg, bb, ee);
                        uint8_t rgbm[4];
                        bh_encode_rgbm(lin, rgbm);

                        const int dx = (int)r.x + lx;
                        const int dy = (int)r.y + ly;
                        if (dx < 0 || dy < 0 || dx >= atlas_w || dy >= atlas_h)
                            continue;

                        const size_t di = ((size_t)dy * (size_t)atlas_w + (size_t)dx) * 4u;
                        atlas_rgba[di + 0] = rgbm[0];
                        atlas_rgba[di + 1] = rgbm[1];
                        atlas_rgba[di + 2] = rgbm[2];
                        atlas_rgba[di + 3] = rgbm[3];
                    }
                }
            }

            char tex_name[256];
            SDL_snprintf(tex_name, (int)sizeof(tex_name), "bsp_lightmap_atlas:%s", bsp_abs_path);
            lightmap_atlas_handle = BH_TextureManager_CreateTextureFromRGBA8(
                sv->textures, tex_name, atlas_rgba, (uint32_t)atlas_w, (uint32_t)atlas_h, (uint32_t)atlas_w * 4u,
                BH_TEXTURE_SEMANTIC_HDR_LIGHTMAP);

            SDL_free(atlas_rgba);
        }
    }

    /* ---------------------------------------------------------------------
       Materials (stubbed albedo fetch)
       --------------------------------------------------------------------- */

    BH_Material **mats = (BH_Material **)BH_Arena_Alloc(level_arena, (size_t)texdata_count * sizeof(*mats), 8);
    if (!mats)
    {
        BH_File_Free(&fd);
        return false;
    }
    SDL_memset(mats, 0, (size_t)texdata_count * sizeof(*mats));

    for (int ti = 0; ti < texdata_count; ++ti)
    {
        const char *tname = bh_texdata_name(texdata, texdata_count, strtable, strtable_count, strdata, (int)len_strdata,
                                            ti);
        if (!tname || !tname[0])
            tname = "<unnamed>";

        BH_Material *m = (BH_Material *)BH_Arena_Alloc(level_arena, sizeof(BH_Material), 8);
        if (!m)
            continue;

        if (!BH_Material_Init(m, tname, sv->materials->fs_refl, level_arena))
            continue;

        /*
            Texture fetch stub:
            For now we just tint the default white texture, and attach our generated lightmap atlas.
        */
        const vec3 tint = bh_tint_from_name(tname);

        m->textures[BH_MATERIAL_TEX_ALBEDO] = BH_TextureManager_GetDefaultWhite(sv->textures);
        m->textures[BH_MATERIAL_TEX_NORMAL] = BH_TextureManager_GetDefaultNormal(sv->textures);
        m->textures[BH_MATERIAL_TEX_ROUGHNESS] = BH_TextureManager_GetDefaultWhite(sv->textures);
        m->textures[BH_MATERIAL_TEX_METALLIC] = BH_TextureManager_GetDefaultBlack(sv->textures);
        m->textures[BH_MATERIAL_TEX_AO] = BH_TextureManager_GetDefaultWhite(sv->textures);

        if (lightmap_atlas_handle)
        {
            m->textures[BH_MATERIAL_TEX_LIGHTMAP] = lightmap_atlas_handle;
            BH_ParamBlock_SetFloat32(&m->fragment_params, "u_LightmapStrength", 1.0f);
        }
        else
        {
            BH_ParamBlock_SetFloat32(&m->fragment_params, "u_LightmapStrength", 0.0f);
        }

        BH_ParamBlock_SetFloat32(&m->fragment_params, "u_LightmapDirStrength", 0.0f);
        BH_ParamBlock_SetFloat32(&m->fragment_params, "u_ShadowmaskStrength", 0.0f);

        BH_ParamBlock_SetVec3(&m->fragment_params, "u_Tint", &tint);
        BH_ParamBlock_SetFloat32(&m->fragment_params, "u_NormalScale", 1.0f);
        BH_ParamBlock_SetFloat32(&m->fragment_params, "u_MetallicFactor", 0.0f);
        BH_ParamBlock_SetFloat32(&m->fragment_params, "u_RoughnessFactor", 1.0f);

        mats[ti] = m;
    }

    /* ---------------------------------------------------------------------
       Geometry: batch faces by TexData
       --------------------------------------------------------------------- */

    typedef struct BH_BspGeomGroup
    {
        int texdata_idx;
        BH_Vertex *verts;
        uint32_t count;
        uint32_t cap;
    } BH_BspGeomGroup;

    BH_BspGeomGroup *groups = (BH_BspGeomGroup *)SDL_malloc((size_t)texdata_count * sizeof(*groups));
    if (!groups)
    {
        BH_File_Free(&fd);
        return false;
    }

    SDL_memset(groups, 0, (size_t)texdata_count * sizeof(*groups));
    for (int i = 0; i < texdata_count; ++i)
    {
        groups[i].texdata_idx = i;
    }

    const int max_face_verts = 32;
    vec3 face_pos[32];

    for (int fi = 0; fi < face_count; ++fi)
    {
        const BH_VBSP_Face *f = &faces[fi];
        if (f->numEdges < 3)
            continue;
        if (f->numEdges > max_face_verts)
            continue;

        const int texinfo_idx = (int)f->texInfo;
        if (texinfo_idx < 0 || texinfo_idx >= texinfo_count)
            continue;

        const BH_VBSP_TexInfo *ti = &texinfo[texinfo_idx];
        const int texdata_idx = (int)ti->texData;
        if (texdata_idx < 0 || texdata_idx >= texdata_count)
            continue;

        /* Optional: skip some tool textures from rendering */
        const char *tname = bh_texdata_name(texdata, texdata_count, strtable, strtable_count, strdata, (int)len_strdata,
                                            texdata_idx);
        if (tname && SDL_strncmp(tname, "TOOLS/", 6) == 0)
        {
            //if (SDL_strncmp(tname, "TOOLS/TOOLSSKYBOX", 17) != 0)
            continue;
        }

        const BH_VBSP_TexData *td = &texdata[texdata_idx];

        /* Face normal from plane */
        const int pl = (int)f->planeNum;
        if (pl < 0 || pl >= plane_count)
            continue;

        vec3 n = vec3_make(planes[pl].normal[0], planes[pl].normal[1], planes[pl].normal[2]);
        if (f->planeSide)
            n = vec3_scale(n, -1.0f);

        /* Collect polygon verts (CW) */
        for (int ei = 0; ei < (int)f->numEdges; ++ei)
        {
            const int se = f->firstEdge + ei;
            if (se < 0 || se >= surfedge_count)
                break;

            int edge_idx = surfedges[se];
            bool rev = false;
            if (edge_idx < 0)
            {
                edge_idx = -edge_idx;
                rev = true;
            }

            if (edge_idx < 0 || edge_idx >= edge_count)
                break;

            const BH_VBSP_Edge e = edges[edge_idx];
            const uint16_t vi = rev ? e.v[1] : e.v[0];
            if (vi >= (uint16_t)vert_count)
                break;

            const BH_VBSP_Vertex v = verts[vi];
            face_pos[ei] = vec3_make(v.position[0], v.position[1], v.position[2]);
        }

        /* Triangulate via fan, reverse winding to CCW */
        const int tri_count = (int)f->numEdges - 2;
        if (tri_count <= 0)
            continue;

        BH_BspGeomGroup *g = &groups[texdata_idx];
        const uint32_t add_verts = (uint32_t)(tri_count * 3);
        if (g->count + add_verts > g->cap)
        {
            uint32_t new_cap = (g->cap == 0) ? 1024u : g->cap;
            while (new_cap < g->count + add_verts)
                new_cap *= 2u;

            BH_Vertex *nv = (BH_Vertex *)SDL_realloc(g->verts, (size_t)new_cap * sizeof(BH_Vertex));
            if (!nv)
                continue;

            g->verts = nv;
            g->cap = new_cap;
        }

        for (int tii = 0; tii < tri_count; ++tii)
        {
            const vec3 p0 = face_pos[0];
            const vec3 p1 = face_pos[tii + 2];
            const vec3 p2 = face_pos[tii + 1];

            const vec2 uv0 = bh_calc_uv(p0, ti, td);
            const vec2 uv1 = bh_calc_uv(p1, ti, td);
            const vec2 uv2 = bh_calc_uv(p2, ti, td);

            const vec2 lm0 = bh_calc_lightmap_uv2(p0, ti, f, fi, lm_regions, face_count, atlas_w, atlas_h);
            const vec2 lm1 = bh_calc_lightmap_uv2(p1, ti, f, fi, lm_regions, face_count, atlas_w, atlas_h);
            const vec2 lm2 = bh_calc_lightmap_uv2(p2, ti, f, fi, lm_regions, face_count, atlas_w, atlas_h);

            g->verts[g->count++] = (BH_Vertex){.position = p0, .normal = n, .uv = uv0, .uv2 = lm0};
            g->verts[g->count++] = (BH_Vertex){.position = p1, .normal = n, .uv = uv1, .uv2 = lm1};
            g->verts[g->count++] = (BH_Vertex){.position = p2, .normal = n, .uv = uv2, .uv2 = lm2};
        }
    }

    /* ---------------------------------------------------------------------
       Create meshes / scene nodes
       --------------------------------------------------------------------- */

    uint32_t mesh_count = 0;
    for (int i = 0; i < texdata_count; ++i)
        if (groups[i].count > 0)
            mesh_count++;

    BH_SceneNode *world_root = bh_create_named_child(scene, scene->root, level_arena, "world");
    BH_StaticGeometryEntity *world_ent = bh_static_geometry_entity_create(level_arena);
    BH_Mesh *meshes = (BH_Mesh *)BH_Arena_Alloc(level_arena, (size_t)mesh_count * sizeof(BH_Mesh), 8);

    if (world_root && world_ent && meshes)
    {
        SDL_memset(meshes, 0, (size_t)mesh_count * sizeof(BH_Mesh));
        world_ent->meshes = meshes;
        world_ent->mesh_count = 0;
        BH_Entity_Attach(scene, (BH_Entity *)world_ent, world_root);

        for (int i = 0; i < texdata_count; ++i)
        {
            BH_BspGeomGroup *g = &groups[i];
            if (g->count == 0)
                continue;

            const BH_Material *mat = (mats && i >= 0 && i < texdata_count && mats[i]) ? mats[i]
                                                                                      : BH_MaterialManager_GetFallback(sv->materials);

            BH_Mesh m = {0};
            if (BH_Mesh_CreateTriangleList(&m, renderer->device, level_arena, g->verts, g->count, mat))
            {
                const uint32_t out_i = world_ent->mesh_count++;
                meshes[out_i] = m;

                char node_name[128];
                const char *tname = bh_texdata_name(texdata, texdata_count, strtable, strtable_count, strdata, (int)len_strdata,
                                                    i);
                if (!tname || !tname[0])
                    tname = "mesh";
                SDL_snprintf(node_name, sizeof(node_name), "mesh_%u_%s", out_i, tname);

                BH_SceneNode *n = bh_create_named_child(scene, world_root, level_arena, node_name);
                if (n)
                    n->mesh = &meshes[out_i];
            }
        }
    }

    for (int i = 0; i < texdata_count; ++i)
    {
        SDL_free(groups[i].verts);
    }
    SDL_free(groups);

    /* ---------------------------------------------------------------------
       Collision brushes
       --------------------------------------------------------------------- */

    BH_TracePlane brush_planes[BH_PHYS_MAX_SIDES];

    for (int bi = 0; bi < brush_count; ++bi)
    {
        const BH_VBSP_Brush *b = &brushes[bi];
        if (b->numSides <= 0)
            continue;

        const int first = b->firstSide;
        const int count = b->numSides;
        if (first < 0 || first + count > brushside_count)
            continue;

        uint32_t plane_out_count = 0;

        for (int si = 0; si < count; ++si)
        {
            const BH_VBSP_BrushSide *s = &brushsides[first + si];
            //if (s->bevel != 0)
            //    continue;

            const int pn = (int)s->planeNum;
            if (pn < 0 || pn >= plane_count)
                continue;

            if (plane_out_count >= BH_PHYS_MAX_SIDES)
                break;

            const BH_VBSP_Plane pl = planes[pn];
            brush_planes[plane_out_count++] = (BH_TracePlane){
                .normal = vec3_make(pl.normal[0], pl.normal[1], pl.normal[2]),
                .dist = pl.dist,
                .type = 3,
            };
        }

        if (plane_out_count >= 4)
        {
            BH_PhysicsAddBrushConvex(physics, brush_planes, plane_out_count, (BH_Contents)b->contents, (uint32_t)bi,
                                     false);
        }
    }

    BH_File_Free(&fd);
    return true;
}
