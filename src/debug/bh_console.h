/* -----------------------------------------------------------------------------
   bh_console.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "core/bh_core.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Types
       ----------------------------------------------------------------------------- */

    typedef bool (*BH_ConsoleCommandFn)(void *ctx, int argc, const char **argv);

    typedef enum BH_ConsoleVarType
    {
        BH_CONSOLE_VAR_INVALID = 369369,
        BH_CONSOLE_VAR_BOOL = 0,
        BH_CONSOLE_VAR_INT = 1,
        BH_CONSOLE_VAR_FLOAT = 2,
        BH_CONSOLE_VAR_STRING = 3,
    } BH_ConsoleVarType;

    typedef uint32_t BH_ConsoleVarId;

    /* -----------------------------------------------------------------------------
       Lifecycle
       ----------------------------------------------------------------------------- */

    void BH_Console_Init(void);
    void BH_Console_Shutdown(void);

    /* -----------------------------------------------------------------------------
       Registration
       ----------------------------------------------------------------------------- */

    bool BH_Console_RegisterCommand(const char *name, const char *help, BH_ConsoleCommandFn fn);

    BH_ConsoleVarId BH_Console_RegisterVarBool(const char *name, bool default_value, const char *help);
    BH_ConsoleVarId BH_Console_RegisterVarInt(const char *name, int default_value, const char *help);
    BH_ConsoleVarId BH_Console_RegisterVarFloat(const char *name, float default_value, const char *help);
    BH_ConsoleVarId BH_Console_RegisterVarString(const char *name, const char *default_value, const char *help);

    /* -----------------------------------------------------------------------------
       Accessors
       ----------------------------------------------------------------------------- */

    BH_ConsoleVarId BH_Console_FindVar(const char *name);
    BH_ConsoleVarType BH_Console_VarType(BH_ConsoleVarId id);

    bool BH_Console_VarGetBool(BH_ConsoleVarId id);
    int BH_Console_VarGetInt(BH_ConsoleVarId id);
    float BH_Console_VarGetFloat(BH_ConsoleVarId id);
    const char *BH_Console_VarGetString(BH_ConsoleVarId id);

    void BH_Console_VarSetBool(BH_ConsoleVarId id, bool v);
    void BH_Console_VarSetInt(BH_ConsoleVarId id, int v);
    void BH_Console_VarSetFloat(BH_ConsoleVarId id, float v);
    void BH_Console_VarSetString(BH_ConsoleVarId id, const char *v);

    /* -----------------------------------------------------------------------------
       Execution & Utilities
       ----------------------------------------------------------------------------- */

    /* Parses and executes command buffer. Returns true if handled. */
    bool BH_Console_Execute(void *ctx, const char *cmdline);

    /* Parses boolean strings (1/0, true/false, on/off). */
    bool BH_Console_ParseBool(const char *s, bool *out);

    /* Dumps command/cvar list to output. */
    void BH_Console_PrintHelp(const char *optional_filter_prefix);

#ifdef __cplusplus
} /* extern "C" */
#endif