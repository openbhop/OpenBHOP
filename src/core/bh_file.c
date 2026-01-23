/* -----------------------------------------------------------------------------
   bh_file.c
   ----------------------------------------------------------------------------- */

#include "bh_file.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------------
    Internal Helpers
    ----------------------------------------------------------------------------- */

static inline bool bh_path_is_sep(char c)
{
    return (c == '/') || (c == '\\');
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_File_ReadAll(const char *path, BH_FileData *out_data)
{
    if (!out_data)
    {
        return false;
    }

    *out_data = (BH_FileData){0};

    if (!path || !path[0])
    {
        return false;
    }

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);

    if (!data)
    {
        return false;
    }

    out_data->data = data;
    out_data->size = size;

    return true;
}

void BH_File_Free(BH_FileData *data)
{
    if (data && data->data)
    {
        SDL_free(data->data);
        *data = (BH_FileData){0};
    }
}

char *BH_File_JoinPath(const char *base, const char *relative)
{
    if (!base)
    {
        base = "";
    }
    if (!relative)
    {
        relative = "";
    }

    const size_t base_len = strlen(base);
    const size_t rel_len = strlen(relative);
    const bool need_sep = (base_len > 0) && !bh_path_is_sep(base[base_len - 1]);
    const size_t out_len = base_len + (need_sep ? 1 : 0) + rel_len;

    char *out = malloc(out_len + 1);
    if (!out)
    {
        return NULL;
    }

    memcpy(out, base, base_len);

    size_t at = base_len;
    if (need_sep)
    {
        out[at++] = '/';
    }

    memcpy(out + at, relative, rel_len);
    out[out_len] = '\0';

    for (size_t i = 0; i < out_len && out[i] != '\0'; ++i)
    {
        if (out[i] == '\\')
        {
            out[i] = '/';
        }
    }

    return out;
}
