/* -----------------------------------------------------------------------------
   bh_file.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "bh_core.h"

typedef struct BH_FileData
{
    void *data;
    size_t size;
} BH_FileData;

/* Reads file to memory. Caller owns data. */
bool BH_File_ReadAll(const char *path, BH_FileData *out_data);

/* Frees file data and zeroes struct. */
void BH_File_Free(BH_FileData *data);

/* Joins paths with normalized separators. Caller frees result. */
char *BH_File_JoinPath(const char *base, const char *relative);
