/* evilwm - Minimalist Window Manager for X
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Debugging macros and support functions.

#ifndef EVILWM_LOG_H__
#define EVILWM_LOG_H__

#if defined(STDIO) || defined(DEBUG) || defined(XDEBUG)
# include <stdio.h>
#endif

#ifdef XDEBUG
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#ifdef DEBUG
extern int log_indent;
#endif

#ifdef STDIO
# define LOG_INFO(...) printf(__VA_ARGS__);
# define LOG_ERROR(...) fprintf(stderr, __VA_ARGS__);
#else
# define LOG_INFO(...)
# define LOG_ERROR(...)
#endif

// Debug macros:
//
// LOG_ENTER(...)   on function entry; prints message, increases indent level
// LOG_LEAVE(...)   on function exit; decreases indent level, prints message
// LOG_DEBUG(...)   print message at current indent level
// LOG_DEBUG_(...)  print continuation message (no indent)

#ifdef DEBUG
# define LOG_INDENT() do { int ii; for (ii = 0; ii < log_indent; ii++) fprintf(stderr, "   "); } while (0)
# define LOG_ENTER(...) do { LOG_INDENT(); log_indent++; fprintf(stderr, __VA_ARGS__); fprintf(stderr, " at %s:%d\n", __FILE__, __LINE__); } while (0)
# define LOG_LEAVE() do { if (log_indent > 0) log_indent--; } while (0)
# define LOG_DEBUG(...) do { LOG_INDENT(); fprintf(stderr, __VA_ARGS__); } while (0)
# define LOG_DEBUG_(...) fprintf(stderr, __VA_ARGS__)
#else
# define LOG_ENTER(...)
# define LOG_LEAVE(...)
# define LOG_DEBUG(...)
# define LOG_DEBUG_(...)
#endif

// X call debugging macros:

#ifdef XDEBUG
# define LOG_XENTER(...) LOG_ENTER(__VA_ARGS__)
# define LOG_XLEAVE(...) LOG_LEAVE(__VA_ARGS__)
# define LOG_XDEBUG(...) LOG_DEBUG(__VA_ARGS__)
# define LOG_XDEBUG_(...) LOG_DEBUG_(__VA_ARGS__)
#else
# define LOG_XENTER(...)
# define LOG_XLEAVE(...)
# define LOG_XDEBUG(...)
# define LOG_XDEBUG_(...)
#endif

#ifdef XDEBUG

// Print window geometry
void debug_window_attributes(XWindowAttributes *attr);
// Dump size hints
void debug_wm_normal_hints(XSizeHints *size);

#else

# define debug_window_attributes(a)
# define debug_wm_normal_hints(s)

#endif

#endif
