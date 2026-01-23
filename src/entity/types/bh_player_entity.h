/* -----------------------------------------------------------------------------
   bh_player_entity.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../../sim/bh_player_controller.h"
#include "../bh_entity.h"

typedef struct BH_PlayerEntity
{
    BH_Entity base;

    BH_PlayerState state;
    BH_PlayerControllerConfig cfg;
} BH_PlayerEntity;

const BH_EntityVTable *bh_player_entity_vtable(void);
BH_PlayerEntity *bh_player_entity_create(BH_Arena *arena);