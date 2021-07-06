/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

static int send_xmessage(Window w, Atom a, long x);

// Client tracking information
struct list *clients_tab_order = NULL;
struct list *clients_mapping_order = NULL;
struct list *clients_stacking_order = NULL;
struct client *current = NULL;

// Get WM_NORMAL_HINTS property.  Populates appropriate parts of the client
// structure and returns the hint flags (which indicates whether sizes or
// positions were user- or program-specified).

long get_wm_normal_hints(struct client *c) {
	long flags;
	long dummy;
	XSizeHints *size = XAllocSizeHints();

	LOG_XENTER("XGetWMNormalHints(window=%lx)", (unsigned long)c->window);
	XGetWMNormalHints(display.dpy, c->window, size, &dummy);
	debug_wm_normal_hints(size);
	LOG_XLEAVE();

	flags = size->flags;

	if (flags & PMinSize) {
		c->min_width = size->min_width;
		c->min_height = size->min_height;
	} else {
		c->min_width = c->min_height = 0;
	}

	if (flags & PMaxSize) {
		c->max_width = size->max_width;
		c->max_height = size->max_height;
	} else {
		c->max_width = c->max_height = 0;
	}

	if (flags & PBaseSize) {
		c->base_width = size->base_width;
		c->base_height = size->base_height;
	} else {
		c->base_width = c->min_width;
		c->base_height = c->min_height;
	}

	c->width_inc = c->height_inc = 1;
	if (flags & PResizeInc) {
		c->width_inc = size->width_inc ? size->width_inc : 1;
		c->height_inc = size->height_inc ? size->height_inc : 1;
	}

	if (!(flags & PMinSize)) {
		c->min_width = c->base_width + c->width_inc;
		c->min_height = c->base_height + c->height_inc;
	}

	if (flags & PWinGravity) {
		c->win_gravity_hint = size->win_gravity;
	} else {
		c->win_gravity_hint = NorthWestGravity;
	}
	c->win_gravity = c->win_gravity_hint;

	XFree(size);
	return flags;
}

// Determine EWMH "window type" and update client flags accordingly.  The only
// windows we currently treat any differently are docks.

void get_window_type(struct client *c) {
	unsigned type = ewmh_get_net_wm_window_type(c->window);
	update_window_type_flags(c, type);
}

void update_window_type_flags(struct client *c, unsigned type) {
	c->is_dock = (type & EWMH_WINDOW_TYPE_DOCK) ? 1 : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Managed windows are all reparented, so most client operations act on the
// parent window.

// find_client() is used all over the place.  Return the client that has
// specified window as either window or parent.  NULL if not found.

struct client *find_client(Window w) {
	struct list *iter;

	for (iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (w == c->parent || w == c->window)
			return c;
	}
	return NULL;
}

// Determine which monitor to consider "current" for the client.
struct monitor *client_monitor(struct client *c) {
	int midx = c->x + c->width/2;
	int midy = c->y + c->height/2;
	_Bool was_inside = 0;
	struct monitor *best = NULL;
	for (int i = 0; i < c->screen->nmonitors; i++) {
		struct monitor *m = &c->screen->monitors[i];
		_Bool is_inside = (midx >= m->x && midx < (m->x + m->width)) &&
		                  (midy >= m->y && midy < (m->y + m->height));
		if (is_inside && !was_inside) {
			best = m;
			was_inside = 1;
			continue;
		}
		// TODO: more heuristics to try and be intelligent about
		// overlapping regions
	}
	if (best)
		return best;
	return &c->screen->monitors[0];
}

// "Hides" the client (unmaps and flags it as iconified).  Used to simulate
// virtual desktops by hiding all clients not on the current vdesk.

void client_hide(struct client *c) {
	c->ignore_unmap++;  // ignore unmap so we don't remove client
	XUnmapWindow(display.dpy, c->parent);
	set_wm_state(c, IconicState);
}

// Show client (and flag it as normal - not iconified).  Used for vdesks and
// initial managing of client.

void client_show(struct client *c) {
	XMapWindow(display.dpy, c->parent);
	set_wm_state(c, NormalState);
}

// Raise client.  Maintains clients_stacking_order list and EWMH hints.

void client_raise(struct client *c) {
	XRaiseWindow(display.dpy, c->parent);
	clients_stacking_order = list_to_tail(clients_stacking_order, c);
	ewmh_set_net_client_list_stacking(c->screen);
}

// Lower client.  Maintains clients_stacking_order list and EWMH hints.

void client_lower(struct client *c) {
	XLowerWindow(display.dpy, c->parent);
	clients_stacking_order = list_to_head(clients_stacking_order, c);
	ewmh_set_net_client_list_stacking(c->screen);
}

// Set window state.  This is either NormalState (visible), IconicState
// (hidden) or WithdrawnState (removing).

void set_wm_state(struct client *c, int state) {
	// Using "long" for the type of "data" looks wrong, but the fine people
	// in the X Consortium defined it this way (even on 64-bit machines).
	long data[2];
	data[0] = state;
	data[1] = None;
	XChangeProperty(display.dpy, c->window, X_ATOM(WM_STATE), X_ATOM(WM_STATE), 32,
			PropModeReplace, (unsigned char *)data, 2);
}

// Inform client of changed geometry by sending ConfigureNotify to its window.

void send_config(struct client *c) {
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.event = c->window;
	ce.window = c->window;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->width;
	ce.height = c->height;
	ce.border_width = 0;
	ce.above = None;
	ce.override_redirect = False;

	XSendEvent(display.dpy, c->window, False, StructureNotifyMask, (XEvent *)&ce);
}

// Offset client to show border according to window's gravity.  e.g.,
// SouthEastGravity will offset the client up and left by the supplied border
// width.

void client_gravitate(struct client *c, int bw) {
	int dx = 0, dy = 0;
	switch (c->win_gravity) {
	default:
	case NorthWestGravity:
		dx = bw;
		dy = bw;
		break;
	case NorthGravity:
		dy = bw;
		break;
	case NorthEastGravity:
		dx = -bw;
		dy = bw;
		break;
	case EastGravity:
		dx = -bw;
		break;
	case CenterGravity:
		break;
	case WestGravity:
		dx = bw;
		break;
	case SouthWestGravity:
		dx = bw;
		dy = -bw;
		break;
	case SouthGravity:
		dy = -bw;
		break;
	case SouthEastGravity:
		dx = -bw;
		dy = -bw;
		break;
	}
	// Don't gravitate if client would be maximised along either axis
	// (unless it's offset already).
	if (c->x != 0 || c->width != DisplayWidth(display.dpy, c->screen->screen)) {
		c->x += dx;
	}
	if (c->y != 0 || c->height != DisplayHeight(display.dpy, c->screen->screen)) {
		c->y += dy;
	}
}

// Activate a client.  Colours its border (and uncolours the
// previously-selected), installs any colourmap, sets input focus and updates
// EWMH properties.

void select_client(struct client *c) {
	struct client *old_current = current;
	if (current)
		XSetWindowBorder(display.dpy, current->parent, current->screen->bg.pixel);
	if (c) {
		unsigned long bpixel;
		if (is_fixed(c))
			bpixel = c->screen->fc.pixel;
		else
			bpixel = c->screen->fg.pixel;
		XSetWindowBorder(display.dpy, c->parent, bpixel);
		XInstallColormap(display.dpy, c->cmap);
		XSetInputFocus(display.dpy, c->window, RevertToPointerRoot, CurrentTime);
	}
	current = c;
	// Update _NET_WM_STATE_FOCUSED for old current and _NET_ACTIVE_WINDOW
	// on its screen root.
	if (old_current)
		ewmh_set_net_wm_state(old_current);
	// Now do same for new current.
	if (c)
		ewmh_set_net_wm_state(c);
}

// Move a client to a specific vdesk.  If that means it should no longer be
// visible, hide it.

void client_to_vdesk(struct client *c, unsigned vdesk) {
	if (valid_vdesk(vdesk)) {
		c->vdesk = vdesk;
		if (c->vdesk == c->screen->vdesk || c->vdesk == VDESK_FIXED) {
			client_show(c);
		} else {
			client_hide(c);
		}
		ewmh_set_net_wm_desktop(c);
		select_client(current);
	}
}

// Stop managing a client.  Undoes any transformations that were made when
// managing it.

void remove_client(struct client *c) {
	LOG_ENTER("remove_client(window=%lx, %s)", (unsigned long)c->window, c->remove ? "withdrawing" : "wm quitting");

	// Grab the server so any X errors are guaranteed to come from our actions.
	XGrabServer(display.dpy);

	// Flag to ignore any X errors we trigger.  The window may well already
	// have been deleted from the server, so anything we try to do to it
	// here would raise one.
	ignore_xerror = 1;

	// ICCCM 4.1.3.1
	// "When the window is withdrawn, the window manager will either change
	//  the state field's value to WithdrawnState or it will remove the
	//  WM_STATE property entirely."
	//
	// EWMH 1.3
	// "The Window Manager should remove the property whenever a window is
	//  withdrawn but it should leave the property in place when it is
	//  shutting down." (both _NET_WM_DESKTOP and _NET_WM_STATE)

	if (c->remove) {
		LOG_DEBUG("setting WithdrawnState\n");
		if (c == current) {
			XSetInputFocus(display.dpy, PointerRoot, RevertToPointerRoot,
			               CurrentTime);
		}
		set_wm_state(c, WithdrawnState);
		ewmh_withdraw_client(c);
	} else {
		ewmh_remove_allowed_actions(c);
	}

	// Undo the geometry changes applied when we managed the client
	client_gravitate(c, -c->border);
	client_gravitate(c, c->old_border);
	c->x -= c->old_border;
	c->y -= c->old_border;

	// Reparent window back to the root
	XReparentWindow(display.dpy, c->window, c->screen->root, c->x, c->y);

	// Restore any old border
	XSetWindowBorderWidth(display.dpy, c->window, c->old_border);

	// Remove window from "save set": we are no longer its parent, so if we
	// die now, the window should be fine.
	XRemoveFromSaveSet(display.dpy, c->window);

	// Destroy parent window
	if (c->parent) {
		XDestroyWindow(display.dpy, c->parent);
	}

	// Remove from the client lists
	clients_tab_order = list_delete(clients_tab_order, c);
	clients_mapping_order = list_delete(clients_mapping_order, c);
	clients_stacking_order = list_delete(clients_stacking_order, c);

	// If the wm is quitting, we'll remove the client list properties
	// soon enough, otherwise, update them:
	if (c->remove) {
		ewmh_set_net_client_list(c->screen);
		ewmh_set_net_client_list_stacking(c->screen);
	}

	// Deselect if this client were previously selected
	if (current == c) {
		current = NULL;
		// Remove _NET_WM_STATE_FOCUSED from client window and
		// _NET_ACTIVE_WINDOW from screen if necessary.
		ewmh_set_net_wm_state(c);
	}
	free(c);

#ifdef DEBUG
	{
		int i = 0;
		for (struct list *iter = clients_tab_order; iter; iter = iter->next)
			i++;
		LOG_DEBUG("free(), window count now %d\n", i);
	}
#endif

	XUngrabServer(display.dpy);
	XSync(display.dpy, False);
	ignore_xerror = 0;
	LOG_LEAVE();
}

// Sends WM_DELETE_WINDOW to a client to tell it to shut down.  If kill_client
// is true, use XKillClient instead (terminates its connection to the server
// forcibly).

void send_wm_delete(struct client *c, int kill_client) {
	int i, n, found = 0;
	Atom *protocols;

	if (!kill_client && XGetWMProtocols(display.dpy, c->window, &protocols, &n)) {
		for (i = 0; i < n; i++)
			if (protocols[i] == X_ATOM(WM_DELETE_WINDOW))
				found++;
		XFree(protocols);
	}
	if (found)
		send_xmessage(c->window, X_ATOM(WM_PROTOCOLS), X_ATOM(WM_DELETE_WINDOW));
	else
		XKillClient(display.dpy, c->window);
}

// Send arbitrary X Event to a client (so long as the argument is a single
// long).

static int send_xmessage(Window w, Atom a, long x) {
	XEvent ev;

	ev.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = a;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = x;
	ev.xclient.data.l[1] = CurrentTime;

	return XSendEvent(display.dpy, w, False, NoEventMask, &ev);
}

#ifdef SHAPE

// Query the shape "extents" applied to a window and duplicate them for the
// parent.

void set_shape(struct client *c) {
	Bool bounding_shaped;
	Bool b;  // dummy
	int i;  // dummy
	unsigned u;  // dummy

	if (!display.have_shape) return;

	// Logic to decide if we have a shaped window cribbed from fvwm-2.5.10.
	// Previous method (more than one rectangle returned from
	// XShapeGetRectangles) worked _most_ of the time.

	if (XShapeQueryExtents(display.dpy, c->window, &bounding_shaped, &i, &i,
				&u, &u, &b, &i, &i, &u, &u) && bounding_shaped) {
		LOG_DEBUG("%d shape extents\n", bounding_shaped);
		XShapeCombineShape(display.dpy, c->parent, ShapeBounding, 0, 0,
				c->window, ShapeBounding, ShapeSet);
	}
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Compiling with -DINFOBANNER enables a client information window

#ifdef INFOBANNER

void create_info_window(struct client *c) {
        display.info_window = XCreateSimpleWindow(display.dpy, c->screen->root, -4, -4, 2, 2,
                        0, c->screen->fg.pixel, c->screen->fg.pixel);
        XMapRaised(display.dpy, display.info_window);
        update_info_window(c);
}

void update_info_window(struct client *c) {
        char *name;
        char buf[27];
        int namew, iwinx, iwiny, iwinw, iwinh;
        int width_inc = c->width_inc, height_inc = c->height_inc;

        if (!display.info_window)
                return;
        snprintf(buf, sizeof(buf), "%dx%d+%d+%d", (c->width-c->base_width)/width_inc,
                (c->height-c->base_height)/height_inc, c->x, c->y);
        iwinw = XTextWidth(display.font, buf, strlen(buf)) + 2;
        iwinh = display.font->max_bounds.ascent + display.font->max_bounds.descent;
        XFetchName(display.dpy, c->window, &name);
        if (name) {
                namew = XTextWidth(display.font, name, strlen(name));
                if (namew > iwinw)
                        iwinw = namew + 2;
                iwinh = iwinh * 2;
        }
        iwinx = c->x + c->border + c->width - iwinw;
        iwiny = c->y - c->border;
        if (iwinx + iwinw > DisplayWidth(display.dpy, c->screen->screen))
                iwinx = DisplayWidth(display.dpy, c->screen->screen) - iwinw;
        if (iwinx < 0)
                iwinx = 0;
        if (iwiny + iwinh > DisplayHeight(display.dpy, c->screen->screen))
                iwiny = DisplayHeight(display.dpy, c->screen->screen) - iwinh;
        if (iwiny < 0)
                iwiny = 0;
        XMoveResizeWindow(display.dpy, display.info_window, iwinx, iwiny, iwinw, iwinh);
        XClearWindow(display.dpy, display.info_window);
        if (name) {
                XDrawString(display.dpy, display.info_window, c->screen->invert_gc,
                                1, iwinh / 2 - 1, name, strlen(name));
                XFree(name);
        }
        XDrawString(display.dpy, display.info_window, c->screen->invert_gc, 1, iwinh - 1,
                        buf, strlen(buf));
}

void remove_info_window(void) {
        if (display.info_window)
                XDestroyWindow(display.dpy, display.info_window);
        display.info_window = None;
}

#endif
