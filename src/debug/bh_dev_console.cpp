#include "bh_dev_console.h"

#include <SDL3/SDL.h>

#include <cstdarg>
#include <mutex>
#include <string>
#include <vector>

namespace
{

// Keep pending lines in a small queue. We don't keep a full history here,
// the UI maintains its own history for rendering.
std::mutex g_mutex;
std::vector<std::string> g_pending;

static void bh_split_and_push_locked(const char *text)
{
    if (!text || !text[0])
        return;

    // Split on '\n' to keep the UI logic simple.
    const char *p = text;
    const char *start = p;
    for (; *p; ++p)
    {
        if (*p == '\n')
        {
            if (p > start)
            {
                g_pending.emplace_back(start, p);
            }
            start = p + 1;
        }
    }
    if (p > start)
    {
        g_pending.emplace_back(start, p);
    }
}

} // namespace

extern "C"
{

    void BH_DevConsole_ClearPending(void)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.clear();
    }

    void BH_DevConsole_Print(const char *line)
    {
        if (!line || !line[0])
            return;

        std::lock_guard<std::mutex> lock(g_mutex);
        bh_split_and_push_locked(line);
    }

    void BH_DevConsole_Printf(const char *fmt, ...)
    {
        if (!fmt || !fmt[0])
            return;

        char buf[2048];

        va_list args;
        va_start(args, fmt);
        SDL_vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        BH_DevConsole_Print(buf);
    }

    void BH_DevConsole_Flush(BH_DevConsoleLineFn fn, void *user)
    {
        if (!fn)
            return;

        std::vector<std::string> local;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_pending.empty())
                return;
            local.swap(g_pending);
        }

        for (const std::string &s : local)
        {
            fn(user, s.c_str());
        }
    }

} // extern "C"
