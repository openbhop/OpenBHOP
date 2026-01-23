#pragma once

#include "core/bh_core.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
      bh_dev_console

      A tiny log queue intended for the in-game developer console UI.

      - Call BH_DevConsole_Print/Printf from anywhere (C or C++).
      - The developer console UI periodically flushes pending lines.

      Notes:
        - Designed for main-thread use, but guarded with a mutex to make it safe
          to log from other threads in the future.
    */

    typedef void (*BH_DevConsoleLineFn)(void *user, const char *line);

    /* Clears all queued (not-yet-flushed) lines. */
    void BH_DevConsole_ClearPending(void);

    /* Enqueue a line (UTF-8). Newlines will be split into multiple lines. */
    void BH_DevConsole_Print(const char *line);

    /* printf-style convenience wrapper (writes a single line). */
    void BH_DevConsole_Printf(const char *fmt, ...);

    /*
      Flush any pending lines to a callback.
      The callback is invoked once per line, in FIFO order.
    */
    void BH_DevConsole_Flush(BH_DevConsoleLineFn fn, void *user);

#ifdef __cplusplus
} // extern "C"
#endif
