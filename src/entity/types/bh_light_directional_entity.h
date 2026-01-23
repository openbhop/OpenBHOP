/* -----------------------------------------------------------------------------
   bh_light_directional_entity.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../bh_entity.h"

/* -----------------------------------------------------------------------------
   Public Types
   ----------------------------------------------------------------------------- */

typedef struct BH_LightDirectionalEntity
{
    BH_Entity base;
} BH_LightDirectionalEntity;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

const BH_EntityVTable *bh_light_directional_entity_vtable(void);
BH_LightDirectionalEntity *bh_light_directional_entity_create(BH_Arena *arena);