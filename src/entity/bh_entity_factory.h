/* -----------------------------------------------------------------------------
   bh_entity_factory.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "bh_entity.h"

/*
    Factory: Maps runtime type strings to entity implementations.
    Decouples runtime content from compile-time types.
*/
BH_Entity *BH_Entity_CreateByType(const char *type_name, BH_Arena *arena);