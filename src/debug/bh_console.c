/* -----------------------------------------------------------------------------
   bh_console.c
   ----------------------------------------------------------------------------- */

#include "bh_console.h"
#include "debug/bh_dev_console.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct BH_ConsoleCommand
{
    char *name;
    char *help;
    BH_ConsoleCommandFn fn;
} BH_ConsoleCommand;

typedef struct BH_ConsoleVar
{
    char *name;
    char *help;
    BH_ConsoleVarType type;

    union {
        bool b;
        int i;
        float f;
        char *s;
    } value;
} BH_ConsoleVar;

typedef struct BH_ConsoleState
{
    bool initialized;

    BH_ConsoleCommand *cmds;
    uint32_t cmd_count;
    uint32_t cmd_cap;

    BH_ConsoleVar *vars;
    uint32_t var_count;
    uint32_t var_cap;
} BH_ConsoleState;

static BH_ConsoleState g_console;

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static int bh_stricmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        const int ca = tolower((unsigned char)*a);
        const int cb = tolower((unsigned char)*b);
        if (ca != cb)
        {
            return ca - cb;
        }
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool bh_starts_with(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (*s++ != *prefix++)
            return false;
    }
    return true;
}

static void *bh_grow_array(void *old_ptr, uint32_t old_cap, uint32_t elem_size, uint32_t min_cap)
{
    uint32_t new_cap = (old_cap == 0) ? 16u : old_cap;
    while (new_cap < min_cap)
    {
        new_cap *= 2u;
    }
    return SDL_realloc(old_ptr, (size_t)new_cap * (size_t)elem_size);
}

static BH_ConsoleCommand *bh_find_command(const char *name)
{
    for (uint32_t i = 0; i < g_console.cmd_count; ++i)
    {
        if (SDL_strcmp(g_console.cmds[i].name, name) == 0)
        {
            return &g_console.cmds[i];
        }
    }
    return NULL;
}

static BH_ConsoleVarId bh_find_var_id(const char *name)
{
    for (uint32_t i = 0; i < g_console.var_count; ++i)
    {
        if (SDL_strcmp(g_console.vars[i].name, name) == 0)
        {
            return (BH_ConsoleVarId)i;
        }
    }
    return BH_CONSOLE_VAR_INVALID;
}

static const char *bh_var_type_name(BH_ConsoleVarType t)
{
    switch (t)
    {
    case BH_CONSOLE_VAR_BOOL:
        return "bool";
    case BH_CONSOLE_VAR_INT:
        return "int";
    case BH_CONSOLE_VAR_FLOAT:
        return "float";
    case BH_CONSOLE_VAR_STRING:
        return "string";
    default:
        return "?";
    }
}

static void bh_print_var_value(const BH_ConsoleVar *v)
{
    switch (v->type)
    {
    case BH_CONSOLE_VAR_BOOL:
        BH_DevConsole_Printf("%s = %s", v->name, v->value.b ? "true" : "false");
        break;
    case BH_CONSOLE_VAR_INT:
        BH_DevConsole_Printf("%s = %d", v->name, v->value.i);
        break;
    case BH_CONSOLE_VAR_FLOAT:
        BH_DevConsole_Printf("%s = %g", v->name, v->value.f);
        break;
    case BH_CONSOLE_VAR_STRING:
        BH_DevConsole_Printf("%s = \"%s\"", v->name, v->value.s ? v->value.s : "");
        break;
    default:
        BH_DevConsole_Printf("%s = <unknown>", v->name);
        break;
    }
}

static bool bh_try_set_var_from_string(BH_ConsoleVar *v, const char *s)
{
    if (!s)
        s = "";

    switch (v->type)
    {
    case BH_CONSOLE_VAR_BOOL: {
        bool b = false;
        if (!BH_Console_ParseBool(s, &b))
        {
            BH_DevConsole_Printf("usage: %s <true|false>", v->name);
            return false;
        }
        v->value.b = b;
        return true;
    }
    case BH_CONSOLE_VAR_INT: {
        char *end = NULL;
        long val = strtol(s, &end, 10);
        if (end == s || (end && *end != '\0'))
        {
            BH_DevConsole_Printf("usage: %s <int>", v->name);
            return false;
        }
        v->value.i = (int)val;
        return true;
    }
    case BH_CONSOLE_VAR_FLOAT: {
        char *end = NULL;
        float val = strtof(s, &end);
        if (end == s || (end && *end != '\0'))
        {
            BH_DevConsole_Printf("usage: %s <float>", v->name);
            return false;
        }
        v->value.f = val;
        return true;
    }
    case BH_CONSOLE_VAR_STRING: {
        if (v->value.s)
        {
            SDL_free(v->value.s);
            v->value.s = NULL;
        }
        v->value.s = SDL_strdup(s);
        return true;
    }
    default:
        break;
    }

    return false;
}

static int bh_tokenize_inplace(char *buf, const char **argv, int argv_cap)
{
    int argc = 0;
    char *p = buf;

    while (*p)
    {
        while (*p && isspace((unsigned char)*p))
        {
            ++p;
        }
        if (!*p)
            break;
        if (argc >= argv_cap)
            break;

        if (*p == '"')
        {
            ++p;
            argv[argc++] = p;
            while (*p && *p != '"')
            {
                ++p;
            }
            if (*p == '"')
            {
                *p = '\0';
                ++p;
            }
        }
        else
        {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p))
            {
                ++p;
            }
            if (*p)
            {
                *p = '\0';
                ++p;
            }
        }
    }

    return argc;
}

static int bh_qsort_cmds(const void *a, const void *b)
{
    const BH_ConsoleCommand *ca = *(const BH_ConsoleCommand *const *)a;
    const BH_ConsoleCommand *cb = *(const BH_ConsoleCommand *const *)b;
    return SDL_strcmp(ca->name, cb->name);
}

static int bh_qsort_vars(const void *a, const void *b)
{
    const BH_ConsoleVar *va = *(const BH_ConsoleVar *const *)a;
    const BH_ConsoleVar *vb = *(const BH_ConsoleVar *const *)b;
    return SDL_strcmp(va->name, vb->name);
}

/* -----------------------------------------------------------------------------
   Command Handlers
   ----------------------------------------------------------------------------- */

static bool bh_cmd_help(void *ctx, int argc, const char **argv)
{
    (void)ctx;
    const char *filter = (argc >= 2) ? argv[1] : NULL;

    BH_Console_PrintHelp(filter);
    BH_DevConsole_Print("Note: 'clear' clears the on-screen console output.");
    return true;
}

static bool bh_cmd_echo(void *ctx, int argc, const char **argv)
{
    (void)ctx;
    if (argc < 2)
    {
        BH_DevConsole_Print("usage: echo <text>");
        return true;
    }

    char buf[2048];
    buf[0] = '\0';

    for (int i = 1; i < argc; ++i)
    {
        SDL_strlcat(buf, argv[i], sizeof(buf));
        if (i + 1 < argc)
        {
            SDL_strlcat(buf, " ", sizeof(buf));
        }
    }

    BH_DevConsole_Print(buf);
    return true;
}

/* -----------------------------------------------------------------------------
   Public API: Lifecycle
   ----------------------------------------------------------------------------- */

void BH_Console_Init(void)
{
    if (g_console.initialized)
    {
        return;
    }
    SDL_zero(g_console);
    g_console.initialized = true;

    (void)BH_Console_RegisterCommand("help", "List commands and variables", &bh_cmd_help);
    (void)BH_Console_RegisterCommand("echo", "Print text", &bh_cmd_echo);
}

void BH_Console_Shutdown(void)
{
    if (!g_console.initialized)
    {
        return;
    }

    for (uint32_t i = 0; i < g_console.cmd_count; ++i)
    {
        SDL_free(g_console.cmds[i].name);
        SDL_free(g_console.cmds[i].help);
    }
    SDL_free(g_console.cmds);

    for (uint32_t i = 0; i < g_console.var_count; ++i)
    {
        SDL_free(g_console.vars[i].name);
        SDL_free(g_console.vars[i].help);
        if (g_console.vars[i].type == BH_CONSOLE_VAR_STRING)
        {
            SDL_free(g_console.vars[i].value.s);
        }
    }
    SDL_free(g_console.vars);

    SDL_zero(g_console);
}

/* -----------------------------------------------------------------------------
   Public API: Registration
   ----------------------------------------------------------------------------- */

bool BH_Console_RegisterCommand(const char *name, const char *help, BH_ConsoleCommandFn fn)
{
    if (!name || !name[0] || !fn)
    {
        return false;
    }
    if (!g_console.initialized)
    {
        BH_Console_Init();
    }

    BH_ConsoleCommand *existing = bh_find_command(name);
    if (existing)
    {
        existing->fn = fn;
        if (help && help[0])
        {
            SDL_free(existing->help);
            existing->help = SDL_strdup(help);
        }
        return true;
    }

    if (g_console.cmd_count + 1 > g_console.cmd_cap)
    {
        void *grown =
            bh_grow_array(g_console.cmds, g_console.cmd_cap, sizeof(BH_ConsoleCommand), g_console.cmd_count + 1);
        if (!grown)
            return false;

        g_console.cmds = (BH_ConsoleCommand *)grown;
        g_console.cmd_cap = (g_console.cmd_cap == 0) ? 16u : (g_console.cmd_cap * 2u);

        /* Ensure we meet requirement if jump was large */
        while (g_console.cmd_cap < g_console.cmd_count + 1)
        {
            g_console.cmd_cap *= 2u;
        }
    }

    BH_ConsoleCommand *c = &g_console.cmds[g_console.cmd_count++];
    SDL_zero(*c);
    c->name = SDL_strdup(name);
    c->help = SDL_strdup((help && help[0]) ? help : "");
    c->fn = fn;
    return true;
}

BH_ConsoleVarId BH_Console_RegisterVarBool(const char *name, bool default_value, const char *help)
{
    if (!name || !name[0])
        return BH_CONSOLE_VAR_INVALID;
    if (!g_console.initialized)
        BH_Console_Init();

    BH_ConsoleVarId id = bh_find_var_id(name);
    if (id != BH_CONSOLE_VAR_INVALID)
    {
        if (g_console.vars[id].type != BH_CONSOLE_VAR_BOOL)
        {
            BH_DevConsole_Printf("[console] var '%s' type mismatch", name);
        }
        return id;
    }

    if (g_console.var_count + 1 > g_console.var_cap)
    {
        void *grown = bh_grow_array(g_console.vars, g_console.var_cap, sizeof(BH_ConsoleVar), g_console.var_count + 1);
        if (!grown)
            return BH_CONSOLE_VAR_INVALID;

        g_console.vars = (BH_ConsoleVar *)grown;
        g_console.var_cap = (g_console.var_cap == 0) ? 32u : (g_console.var_cap * 2u);
        while (g_console.var_cap < g_console.var_count + 1)
        {
            g_console.var_cap *= 2u;
        }
    }

    BH_ConsoleVar *v = &g_console.vars[g_console.var_count];
    SDL_zero(*v);
    v->name = SDL_strdup(name);
    v->help = SDL_strdup((help && help[0]) ? help : "");
    v->type = BH_CONSOLE_VAR_BOOL;
    v->value.b = default_value;

    return (BH_ConsoleVarId)g_console.var_count++;
}

BH_ConsoleVarId BH_Console_RegisterVarInt(const char *name, int default_value, const char *help)
{
    if (!name || !name[0])
        return BH_CONSOLE_VAR_INVALID;
    if (!g_console.initialized)
        BH_Console_Init();

    BH_ConsoleVarId id = bh_find_var_id(name);
    if (id != BH_CONSOLE_VAR_INVALID)
    {
        if (g_console.vars[id].type != BH_CONSOLE_VAR_INT)
        {
            BH_DevConsole_Printf("[console] var '%s' type mismatch", name);
        }
        return id;
    }

    if (g_console.var_count + 1 > g_console.var_cap)
    {
        void *grown = bh_grow_array(g_console.vars, g_console.var_cap, sizeof(BH_ConsoleVar), g_console.var_count + 1);
        if (!grown)
            return BH_CONSOLE_VAR_INVALID;

        g_console.vars = (BH_ConsoleVar *)grown;
        g_console.var_cap = (g_console.var_cap == 0) ? 32u : (g_console.var_cap * 2u);
        while (g_console.var_cap < g_console.var_count + 1)
        {
            g_console.var_cap *= 2u;
        }
    }

    BH_ConsoleVar *v = &g_console.vars[g_console.var_count];
    SDL_zero(*v);
    v->name = SDL_strdup(name);
    v->help = SDL_strdup((help && help[0]) ? help : "");
    v->type = BH_CONSOLE_VAR_INT;
    v->value.i = default_value;

    return (BH_ConsoleVarId)g_console.var_count++;
}

BH_ConsoleVarId BH_Console_RegisterVarFloat(const char *name, float default_value, const char *help)
{
    if (!name || !name[0])
        return BH_CONSOLE_VAR_INVALID;
    if (!g_console.initialized)
        BH_Console_Init();

    BH_ConsoleVarId id = bh_find_var_id(name);
    if (id != BH_CONSOLE_VAR_INVALID)
    {
        if (g_console.vars[id].type != BH_CONSOLE_VAR_FLOAT)
        {
            BH_DevConsole_Printf("[console] var '%s' type mismatch", name);
        }
        return id;
    }

    if (g_console.var_count + 1 > g_console.var_cap)
    {
        void *grown = bh_grow_array(g_console.vars, g_console.var_cap, sizeof(BH_ConsoleVar), g_console.var_count + 1);
        if (!grown)
            return BH_CONSOLE_VAR_INVALID;

        g_console.vars = (BH_ConsoleVar *)grown;
        g_console.var_cap = (g_console.var_cap == 0) ? 32u : (g_console.var_cap * 2u);
        while (g_console.var_cap < g_console.var_count + 1)
        {
            g_console.var_cap *= 2u;
        }
    }

    BH_ConsoleVar *v = &g_console.vars[g_console.var_count];
    SDL_zero(*v);
    v->name = SDL_strdup(name);
    v->help = SDL_strdup((help && help[0]) ? help : "");
    v->type = BH_CONSOLE_VAR_FLOAT;
    v->value.f = default_value;

    return (BH_ConsoleVarId)g_console.var_count++;
}

BH_ConsoleVarId BH_Console_RegisterVarString(const char *name, const char *default_value, const char *help)
{
    if (!name || !name[0])
        return BH_CONSOLE_VAR_INVALID;
    if (!g_console.initialized)
        BH_Console_Init();

    BH_ConsoleVarId id = bh_find_var_id(name);
    if (id != BH_CONSOLE_VAR_INVALID)
    {
        if (g_console.vars[id].type != BH_CONSOLE_VAR_STRING)
        {
            BH_DevConsole_Printf("[console] var '%s' type mismatch", name);
        }
        return id;
    }

    if (g_console.var_count + 1 > g_console.var_cap)
    {
        void *grown = bh_grow_array(g_console.vars, g_console.var_cap, sizeof(BH_ConsoleVar), g_console.var_count + 1);
        if (!grown)
            return BH_CONSOLE_VAR_INVALID;

        g_console.vars = (BH_ConsoleVar *)grown;
        g_console.var_cap = (g_console.var_cap == 0) ? 32u : (g_console.var_cap * 2u);
        while (g_console.var_cap < g_console.var_count + 1)
        {
            g_console.var_cap *= 2u;
        }
    }

    BH_ConsoleVar *v = &g_console.vars[g_console.var_count];
    SDL_zero(*v);
    v->name = SDL_strdup(name);
    v->help = SDL_strdup((help && help[0]) ? help : "");
    v->type = BH_CONSOLE_VAR_STRING;
    v->value.s = SDL_strdup(default_value ? default_value : "");

    return (BH_ConsoleVarId)g_console.var_count++;
}

/* -----------------------------------------------------------------------------
   Public API: Accessors
   ----------------------------------------------------------------------------- */

BH_ConsoleVarId BH_Console_FindVar(const char *name)
{
    if (!g_console.initialized)
        return BH_CONSOLE_VAR_INVALID;
    return bh_find_var_id(name);
}

BH_ConsoleVarType BH_Console_VarType(BH_ConsoleVarId id)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
    {
        return (BH_ConsoleVarType)-1;
    }
    return g_console.vars[id].type;
}

bool BH_Console_VarGetBool(BH_ConsoleVarId id)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return false;
    if (g_console.vars[id].type != BH_CONSOLE_VAR_BOOL)
        return false;
    return g_console.vars[id].value.b;
}

int BH_Console_VarGetInt(BH_ConsoleVarId id)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return 0;
    if (g_console.vars[id].type != BH_CONSOLE_VAR_INT)
        return 0;
    return g_console.vars[id].value.i;
}

float BH_Console_VarGetFloat(BH_ConsoleVarId id)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return 0.0f;
    if (g_console.vars[id].type != BH_CONSOLE_VAR_FLOAT)
        return 0.0f;
    return g_console.vars[id].value.f;
}

const char *BH_Console_VarGetString(BH_ConsoleVarId id)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return "";
    if (g_console.vars[id].type != BH_CONSOLE_VAR_STRING)
        return "";
    return g_console.vars[id].value.s ? g_console.vars[id].value.s : "";
}

void BH_Console_VarSetBool(BH_ConsoleVarId id, bool v)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return;
    if (g_console.vars[id].type == BH_CONSOLE_VAR_BOOL)
    {
        g_console.vars[id].value.b = v;
    }
}

void BH_Console_VarSetInt(BH_ConsoleVarId id, int v)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return;
    if (g_console.vars[id].type == BH_CONSOLE_VAR_INT)
    {
        g_console.vars[id].value.i = v;
    }
}

void BH_Console_VarSetFloat(BH_ConsoleVarId id, float v)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return;
    if (g_console.vars[id].type == BH_CONSOLE_VAR_FLOAT)
    {
        g_console.vars[id].value.f = v;
    }
}

void BH_Console_VarSetString(BH_ConsoleVarId id, const char *v)
{
    if (id == BH_CONSOLE_VAR_INVALID || id >= g_console.var_count)
        return;
    if (g_console.vars[id].type == BH_CONSOLE_VAR_STRING)
    {
        BH_ConsoleVar *var = &g_console.vars[id];
        if (var->value.s)
        {
            SDL_free(var->value.s);
            var->value.s = NULL;
        }
        var->value.s = SDL_strdup(v ? v : "");
    }
}

/* -----------------------------------------------------------------------------
   Public API: Execution & Help
   ----------------------------------------------------------------------------- */

bool BH_Console_ParseBool(const char *s, bool *out)
{
    if (!out)
        return false;
    *out = false;
    if (!s)
        return false;

    if (bh_stricmp(s, "1") == 0 || bh_stricmp(s, "true") == 0 || bh_stricmp(s, "on") == 0 || bh_stricmp(s, "yes") == 0)
    {
        *out = true;
        return true;
    }
    if (bh_stricmp(s, "0") == 0 || bh_stricmp(s, "false") == 0 || bh_stricmp(s, "off") == 0 || bh_stricmp(s, "no") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

bool BH_Console_Execute(void *ctx, const char *cmdline)
{
    if (!g_console.initialized)
        BH_Console_Init();
    if (!cmdline)
        return false;

    char buf[2048];
    SDL_strlcpy(buf, cmdline, sizeof(buf));

    const char *argv[64];
    const int argc = bh_tokenize_inplace(buf, argv, (int)BH_ARRAY_COUNT(argv));
    if (argc <= 0)
        return false;

    if (argc == 1)
    {
        const char *eq = SDL_strchr(argv[0], '=');
        if (eq && eq != argv[0])
        {
            char name[256];
            char value[768];
            size_t nlen = (size_t)(eq - argv[0]);

            if (nlen >= sizeof(name))
                nlen = sizeof(name) - 1u;

            SDL_memcpy(name, argv[0], nlen);
            name[nlen] = '\0';
            SDL_strlcpy(value, eq + 1, sizeof(value));

            BH_ConsoleVarId id = bh_find_var_id(name);
            if (id != BH_CONSOLE_VAR_INVALID)
            {
                BH_ConsoleVar *v = &g_console.vars[id];
                if (bh_try_set_var_from_string(v, value))
                {
                    bh_print_var_value(v);
                    return true;
                }
                return true;
            }
        }
    }

    BH_ConsoleCommand *c = bh_find_command(argv[0]);
    if (c)
    {
        (void)c->fn(ctx, argc, argv);
        return true;
    }

    BH_ConsoleVarId vid = bh_find_var_id(argv[0]);
    if (vid != BH_CONSOLE_VAR_INVALID)
    {
        BH_ConsoleVar *v = &g_console.vars[vid];
        if (argc == 1)
        {
            bh_print_var_value(v);
            return true;
        }
        if (bh_try_set_var_from_string(v, argv[1]))
        {
            bh_print_var_value(v);
        }
        return true;
    }

    (void)ctx;
    return false;
}

void BH_Console_PrintHelp(const char *optional_filter_prefix)
{
    if (!g_console.initialized)
        BH_Console_Init();

    BH_DevConsole_Print("Commands:");

    if (g_console.cmd_count == 0)
    {
        BH_DevConsole_Print("  (none)");
    }
    else
    {
        BH_ConsoleCommand **sorted_cmds =
            (BH_ConsoleCommand **)SDL_malloc(sizeof(BH_ConsoleCommand *) * (size_t)g_console.cmd_count);
        if (sorted_cmds)
        {
            for (uint32_t i = 0; i < g_console.cmd_count; ++i)
            {
                sorted_cmds[i] = &g_console.cmds[i];
            }
            qsort(sorted_cmds, (size_t)g_console.cmd_count, sizeof(BH_ConsoleCommand *), bh_qsort_cmds);

            for (uint32_t i = 0; i < g_console.cmd_count; ++i)
            {
                BH_ConsoleCommand *c = sorted_cmds[i];
                if (optional_filter_prefix && optional_filter_prefix[0] &&
                    !bh_starts_with(c->name, optional_filter_prefix))
                {
                    continue;
                }
                BH_DevConsole_Printf("  %s - %s", c->name, (c->help && c->help[0]) ? c->help : "");
            }
            SDL_free(sorted_cmds);
        }
    }

    BH_DevConsole_Print("Variables:");

    if (g_console.var_count == 0)
    {
        BH_DevConsole_Print("  (none)");
    }
    else
    {
        BH_ConsoleVar **sorted_vars =
            (BH_ConsoleVar **)SDL_malloc(sizeof(BH_ConsoleVar *) * (size_t)g_console.var_count);
        if (sorted_vars)
        {
            for (uint32_t i = 0; i < g_console.var_count; ++i)
            {
                sorted_vars[i] = &g_console.vars[i];
            }
            qsort(sorted_vars, (size_t)g_console.var_count, sizeof(BH_ConsoleVar *), bh_qsort_vars);

            for (uint32_t i = 0; i < g_console.var_count; ++i)
            {
                BH_ConsoleVar *v = sorted_vars[i];
                if (optional_filter_prefix && optional_filter_prefix[0] &&
                    !bh_starts_with(v->name, optional_filter_prefix))
                {
                    continue;
                }
                BH_DevConsole_Printf("  %s (%s) - %s", v->name, bh_var_type_name(v->type),
                                     (v->help && v->help[0]) ? v->help : "");
            }
            SDL_free(sorted_vars);
        }
    }
}