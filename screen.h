/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Screen management.
//
// One evilwm process can manage one display, but a display can comprise
// multiple screens.  Each screen has its own root window and generally,
// clients can't move from one to the other.
//
// Xinerama and Xrandr extensions may allow combining multiple monitors into
// one logical "screen".

#ifndef EVILWM_SCREEN_H_
#define EVILWM_SCREEN_H_

#include <X11/X.h>
#include <X11/Xlib.h>

struct screen {
	int screen;          // screen index for display
	char *display;       // DISPLAY string, eg ":0.0"
	Window root;         // root window of screen
	Window supporting;   // dummy window for EWMH
	Window active;       // current _NET_ACTIVE_WINDOW value for root
	GC invert_gc;        // used to draw outlines
	XColor fg, bg, fc;   // allocated colours; active, inactive, fixed
	unsigned vdesk;      // current vdesk for screen
	unsigned old_vdesk;  // previous vdesk, so user may toggle back to it
	int docks_visible;   // docks can be toggled visible/hidden
};

// Setup and shutdown.
void screen_init(struct screen *s);
void screen_deinit(struct screen *s);

// Switch vdesks; hides & shows clients accordingly.
void switch_vdesk(struct screen *s, unsigned v);

// Show or hide docks.
void set_docks_visible(struct screen *s, int is_visible);

// Xrandr allows a screen to resize; this function adjusts the position of
// clients so they remain visible.
void fix_screen_after_resize(struct screen *s, int oldw, int oldh);

// Find screen corresponding to specified root window.
struct screen *find_screen(Window root);

// Find screen corresponding to the root window the pointer is currently on.
struct screen *find_current_screen(void);

// Grab all the keys we're interested in for the specified screen.
void grab_keys_for_screen(struct screen *s);

#endif
