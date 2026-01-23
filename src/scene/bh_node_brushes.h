/* -----------------------------------------------------------------------------
   bh_node_brushes.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"
#include "../math/bh_math.h"
#include "../physics/bh_physics.h"

struct BH_SceneNode;
struct BH_PhysicsWorld;

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Brush Geometry Types
       ----------------------------------------------------------------------------- */

    /*
       Map-loader agnostic convex brush representation.
       Data stored in world-space.
    */

    typedef struct BH_BrushFaceGeom
    {
        BH_TracePlane plane;
        bool bevel;
        int32_t material_index;

        /* Texture axes/params */
        vec3 u_axis;
        vec3 v_axis;
        float u_shift;
        float v_shift;
        float u_scale;
        float v_scale;
        float rotation_deg;

        /* Indices into BH_BrushGeom::verts */
        uint32_t vert_count;
        uint32_t *verts;
    } BH_BrushFaceGeom;

    typedef struct BH_BrushGeom
    {
        int32_t brush_uid;
        int32_t group_id;
        int32_t entity_uid;

        /* Broad-phase AABB (World Space) */
        vec3 mins;
        vec3 maxs;

        /* Convex Polygon Soup */
        uint32_t vert_count;
        vec3 *verts;

        uint32_t face_count;
        BH_BrushFaceGeom *faces;

        /* Collision Planes (Half-space) */
        uint32_t plane_count;
        BH_TracePlane *planes;

        /* Physics Integration State */
        int physics_brush_index; /* -1 if unregistered */
        BH_Contents physics_contents;
        bool physics_registered;
    } BH_BrushGeom;

    typedef struct BH_NodeBrushLink
    {
        BH_BrushGeom *brush;
        struct BH_NodeBrushLink *next;
    } BH_NodeBrushLink;

    typedef struct BH_NodeBrushes
    {
        BH_NodeBrushLink *first;
        uint32_t count;

        /* Union AABB */
        bool has_bounds;
        vec3 mins;
        vec3 maxs;
    } BH_NodeBrushes;

    /* -----------------------------------------------------------------------------
       Public API
       ----------------------------------------------------------------------------- */

    /*
       Attaches brush geometry to a node.
       Allocates container and links from arena.
       Updates node world bounds.
    */
    bool BH_NodeBrushes_AttachBrush(struct BH_SceneNode *node, BH_BrushGeom *brush, BH_Arena *arena);

    /* Returns pointer to brush with matching UID, or NULL. */
    const BH_BrushGeom *BH_NodeBrushes_FindByUID(const BH_NodeBrushes *nb, int32_t brush_uid);

    /* Registers convex collision brush. Returns physics index or -1 on failure. */
    int BH_BrushGeom_AddToPhysics(BH_BrushGeom *b, struct BH_PhysicsWorld *world, BH_Contents contents,
                                  bool add_bevels_optional);

#ifdef __cplusplus
}
#endif
