#pragma once

#include "bh_debug_draw.h"

typedef struct BH_DbgBatches
{
    const BH_DbgVertex *line_depth;
    uint32_t line_depth_count;

    const BH_DbgVertex *line_always;
    uint32_t line_always_count;

    const BH_DbgVertex *tri_depth;
    uint32_t tri_depth_count;

    const BH_DbgVertex *tri_always;
    uint32_t tri_always_count;
} BH_DbgBatches;

void BH_DebugDraw_GetBatches(BH_DebugDraw *dd, BH_DbgBatches *out);
