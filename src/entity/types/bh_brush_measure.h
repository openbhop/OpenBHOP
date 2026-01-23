#pragma once

#include "../bh_entity.h"

#include "../../render/bh_mesh.h"

typedef struct BH_BrushMeasure
{
    BH_Entity base;
} BH_BrushMeasure;

const BH_EntityVTable *bh_brush_measure_vtable(void);

/* Convenience helper for programmatic spawning. */
BH_BrushMeasure *bh_brush_measure_create(BH_Arena *arena);
