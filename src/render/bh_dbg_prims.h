#pragma once

#include "../core/bh_arena.h"

typedef struct BH_DebugDraw BH_DebugDraw;
typedef struct BH_Renderer BH_Renderer;

bool BH_DbgPrims_Attach(BH_DebugDraw *dd, BH_Renderer *renderer, const char *asset_root, BH_Arena *permanent_arena);
void BH_DbgPrims_Detach(BH_DebugDraw *dd);
