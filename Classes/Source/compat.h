
#ifndef COMPAT_H
#define COMPAT_H

// Definitions for Purr Data compatibility which still has an older API than
// current vanilla.

#include "m_pd.h"
#include "g_canvas.h"

#ifndef IHEIGHT
// Purr Data doesn't have these, hopefully the vanilla values will work
#define IHEIGHT 3       /* height of an inlet in pixels */
#define OHEIGHT 3       /* height of an outlet in pixels */
#endif

#ifdef PDL2ORK
#ifdef no_legacy_warnings
// option to silence legacy warnings by turning sys_gui/sys_vgui into no-ops
#undef sys_vgui
#undef sys_gui
#define sys_vgui(...)
#define sys_gui(s)
#endif
/* This call is needed for the gui parts, which haven't been ported to JS yet
   anyway, so we just make this a no-op for now. XXXFIXME: Once the gui
   features have been ported, we need to figure out what exactly is needed
   here, and implement it in terms of Purr Data's undo/redo system. */
#define pd_undo_set_objectstate(canvas, x, s, undo_argc, undo_argv, redo_argc, redo_argv) /* no-op  */
// zoom factor should always be 1 in Purr Data
#define __zoom(x) 1
#else
#define __zoom(x) x
#endif

#endif
