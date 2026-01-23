/* -----------------------------------------------------------------------------
   bh_scene.c
   ----------------------------------------------------------------------------- */

#include "bh_scene.h"

#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static const char *bh_arena_strdup(BH_Arena *arena, const char *s)
{
    const size_t len = strlen(s);
    char *dst = (char *)BH_Arena_Alloc(arena, len + 1, 1);

    if (dst)
    {
        memcpy(dst, s, len);
        dst[len] = '\0';
    }
    return dst;
}

static void bh_scene_traverse_recursive(const BH_SceneNode *node, float alpha, const mat4 *parent_world,
                                        BH_SceneVisitFn fn, void *user)
{
    const BH_Transform interp = BH_Transform_Lerp(&node->local_prev, &node->local_curr, alpha);
    const mat4 local_m = BH_Transform_GetMat4(&interp);
    const mat4 world_m = parent_world ? mat4_mul(*parent_world, local_m) : local_m;

    if (fn)
    {
        fn(node, &world_m, user);
    }

    for (const BH_SceneNode *c = node->first_child; c; c = c->next_sibling)
    {
        bh_scene_traverse_recursive(c, alpha, &world_m, fn, user);
    }
}

/* -----------------------------------------------------------------------------
   Math / Transform API
   ----------------------------------------------------------------------------- */

BH_Transform BH_Transform_GetIdentity(void)
{
    return (BH_Transform){.position = {0, 0, 0}, .rotation = {0, 0, 0, 1}, .scale = {1, 1, 1}};
}

BH_Transform BH_Transform_Lerp(const BH_Transform *a, const BH_Transform *b, float t)
{
    if (!a || !b)
    {
        return BH_Transform_GetIdentity();
    }

    return (BH_Transform){.position =
                              {
                                  lerpf(a->position.x, b->position.x, t),
                                  lerpf(a->position.y, b->position.y, t),
                                  lerpf(a->position.z, b->position.z, t),
                              },
                          .scale =
                              {
                                  lerpf(a->scale.x, b->scale.x, t),
                                  lerpf(a->scale.y, b->scale.y, t),
                                  lerpf(a->scale.z, b->scale.z, t),
                              },
                          .rotation = quat_nlerp(a->rotation, b->rotation, t)};
}

mat4 BH_Transform_GetMat4(const BH_Transform *t)
{
    if (!t)
    {
        return mat4_identity();
    }
    return mat4_trs(t->position, t->rotation, t->scale);
}

/* -----------------------------------------------------------------------------
   Scene Lifecycle
   ----------------------------------------------------------------------------- */

bool BH_Scene_Init(BH_Scene *scene, BH_Arena *permanent_arena)
{
    if (!scene || !permanent_arena)
    {
        return false;
    }

    *scene = (BH_Scene){
        .root = BH_Scene_CreateNode(permanent_arena, "Root"),
        .entities = NULL,
        .has_directional_light = false,
        .directional_light = {.direction = {0.35f, 0.25f, 1.0f}, .color = {1.0f, 1.0f, 1.0f}, .intensity = 3.0f}};

    return (scene->root != NULL);
}

BH_SceneNode *BH_Scene_CreateNode(BH_Arena *permanent_arena, const char *name)
{
    if (!permanent_arena)
    {
        return NULL;
    }

    BH_SceneNode *n = (BH_SceneNode *)BH_Arena_Alloc(permanent_arena, sizeof(BH_SceneNode), 8);
    if (!n)
    {
        return NULL;
    }

    *n = (BH_SceneNode){.name = bh_arena_strdup(permanent_arena, name ? name : "Node"),
                        .local_prev = BH_Transform_GetIdentity(),
                        .local_curr = BH_Transform_GetIdentity()};

    return n;
}

void BH_Scene_AddChild(BH_SceneNode *parent, BH_SceneNode *child)
{
    if (!parent || !child)
    {
        return;
    }

    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
}

void BH_Scene_SetNodeLocalTransform(BH_SceneNode *node, BH_Transform prev, BH_Transform curr)
{
    if (!node)
    {
        return;
    }
    node->local_prev = prev;
    node->local_curr = curr;
}

void BH_Scene_Traverse(const BH_Scene *scene, float alpha, BH_SceneVisitFn fn, void *user)
{
    if (!scene || !scene->root)
    {
        return;
    }

    const float t = clampf(alpha, 0.0f, 1.0f);
    bh_scene_traverse_recursive(scene->root, t, NULL, fn, user);
}