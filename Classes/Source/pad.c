// porres 2020

#include "m_pd.h"
#include "g_canvas.h"

#include "compat.h"

static t_class *pad_class, *edit_proxy_class;
static t_widgetbehavior pad_widgetbehavior;

typedef struct _edit_proxy{
    t_object    p_obj;
    t_symbol   *p_sym;
    t_clock    *p_clock;
    struct      _pad *p_cnv;
}t_edit_proxy;

typedef struct _pad{
    t_object        x_obj;
    t_glist        *x_glist;
    t_edit_proxy   *x_proxy;
    t_symbol       *x_bindname;
#ifdef PURR_DATA
    t_symbol       *x_mouseclick;
    int             x_mousestate, x_mousebind;
    int             x_mousex, x_mousey;
#endif
    int             x_x;
    int             x_y;
    int             x_w;
    int             x_h;
    int             x_sel;
    int             x_zoom;
    int             x_edit;
    unsigned char   x_color[3];
}t_pad;

#ifdef PURR_DATA
// This is needed to get access to the widget callbacks in struct _class.
// In particular, we need w_getrectfn below.
#include "m_imp.h"
#endif

static void pad_erase(t_pad* x, t_glist* glist){
#ifdef PURR_DATA
    t_canvas *canvas=glist_getcanvas(glist);
    gui_vmess("gui_gobj_erase", "xx", canvas, x);
    if (x->x_mousebind) {
      pd_unbind(&x->x_proxy->p_obj.ob_pd, x->x_mouseclick);
      x->x_mousebind = 0;
    }
#else
    sys_vgui(".x%lx.c delete %lxALL\n", glist_getcanvas(glist), x);
#endif
}

static void pad_draw_io_let(t_pad *x){
    if(x->x_edit){
#ifdef PURR_DATA
        // pad shows its single inlet/outlet pair only in edit mode.
        t_canvas *canvas=glist_getcanvas(x->x_glist);
        /* This is needed to get access to the widget callbacks below.
           Specifically, we use w_getrectfn which gives us the coordinates of
           the object on the canvas. */
        t_class *c = pd_class((t_pd *)x);
        int x1,y1,x2,y2;
        /* We also need a tag for the iolets which become their ids in the
           DOM. If the object was a "text responder" (rtext), we'd go to some
           lengths here, using glist_findrtext to determine the tag as follows
           (code mostly pilfered from g_all_guis.c): */
#if 0
        // recorded here since we might need it for more complex objects later
        t_object *ob = pd_checkobject(&((t_gobj*)x)->g_pd);
        t_rtext *y = glist_findrtext(canvas, ob);
        const char *tag = rtext_gettag(y);
#endif
        /* But this isn't an rtext, just a simple rectangle which responds to
           mouse events, so we can just use a tag derived from the object's
           pointer value here, mimicking what gui_vmess does when it passes a
           pointer value. gui_gobj_draw_io automatically adds "i0" or "o0" to
           the tag signifying an in- or outlet, as is done for the other
           objects (atom, iemgui, etc.). */
        char tagbuf[MAXPDSTRING];
        sprintf(tagbuf, "x%lx", (t_uint)x);
        c->c_wb->w_getrectfn((t_gobj *)x,canvas,&x1,&y1,&x2,&y2);
        gui_vmess("gui_gobj_draw_io", "xxsiiiiiisiii", canvas,
                  x, tagbuf,
                  x1, y1, x1 + IOWIDTH, y1 + IHEIGHT, x1, y1, "i", 0,
                  0, 0);
        gui_vmess("gui_gobj_draw_io", "xxsiiiiiisiii", canvas,
                  x, tagbuf,
                  x1, y2 - IHEIGHT, x1 + IOWIDTH, y2, x1, y1, "o", 0,
                  0, 0);
#else
        t_canvas *cv = glist_getcanvas(x->x_glist);
        int xpos = text_xpix(&x->x_obj, x->x_glist), ypos = text_ypix(&x->x_obj, x->x_glist);
        sys_vgui(".x%lx.c create rectangle %d %d %d %d -fill black -tags [list %lx_io %lxALL]\n",
            cv, xpos, ypos, xpos+(IOWIDTH*x->x_zoom), ypos+(IHEIGHT*x->x_zoom), x, x);
        sys_vgui(".x%lx.c create rectangle %d %d %d %d -fill black -tags [list %lx_io %lxALL]\n",
            cv, xpos, ypos+x->x_h*x->x_zoom, xpos+IOWIDTH*x->x_zoom, ypos+x->x_h*x->x_zoom-IHEIGHT*x->x_zoom, x, x);
#endif
    }
}

static void pad_erase_io_let(t_pad *x){
#ifdef PURR_DATA
    t_canvas *canvas=glist_getcanvas(x->x_glist);
    char tagbuf[MAXPDSTRING];
    /* This gets rid of the iolet rectangles again. No automatic adding of i0
       or o0 here, we have to add those ourselves. XXXTODO: This should be
       simpler. Maybe just give the id of the parent node and have the JS
       function find all children ending in i%d / o%d and delete them all in
       one go. */
    sprintf(tagbuf, "x%lxi0", (t_uint)x);
    gui_vmess("gui_gobj_erase_io", "xs", canvas, tagbuf);
    sprintf(tagbuf, "x%lxo0", (t_uint)x);
    gui_vmess("gui_gobj_erase_io", "xs", canvas, tagbuf);
#else
    t_canvas *cv = glist_getcanvas(x->x_glist);
    sys_vgui(".x%lx.c delete %lx_io\n", cv, x);
#endif
}

#ifdef PURR_DATA
void else_pad_draw_new(t_pad *x)
{
    t_canvas *canvas=glist_getcanvas(x->x_glist);
    t_class *c = pd_class((t_pd *)x);
    int x1, y1, x2, y2;
    char colorbuf[MAXPDSTRING];
    // hex colorspec for the pad's background color
    sprintf(colorbuf, "#%2.2x%2.2x%2.2x", x->x_color[0], x->x_color[1], x->x_color[2]);
    // get the pad's rectangle
    c->c_wb->w_getrectfn((t_gobj *)x, x->x_glist, &x1, &y1, &x2, &y2);
    /* All gui objects start as a gobj, we use gui_gobj_new from pdgui.js to
       create it. This function takes the canvas and the object pointer itself
       as arguments, both become strings in the JS interface which are used to
       find the canvas and the object in the DOM. NOTE: "else_pad" (the type
       argument to gui_gobj_new) becomes the class of the gobj, so that it
       can be themed using CSS if wanted. */
    gui_vmess("gui_gobj_new", "xxsiiii", canvas, x,
        "else_pad", x1, y1, glist_istoplevel(x->x_glist), 0);
    /* Now draw the actual shape of the object. In this case it's just a
       rectangle filled with the given background color which gets added to
       the previously created gobj. NOTE: This needs a JS function in pdgui.js
       which actually creates the shape for us (look for the ELSE section in
       pdgui.js, gui_else_draw_pad can be found there). */
    gui_vmess("gui_else_draw_pad", "xxsii",
        canvas,
        x,
        colorbuf,
        x2 - x1,
        y2 - y1);
}
#endif

static void pad_draw(t_pad *x, t_glist *glist){
#ifdef PURR_DATA
    else_pad_draw_new(x);
#else
    int xpos = text_xpix(&x->x_obj, glist), ypos = text_ypix(&x->x_obj, glist);
    sys_vgui(".x%lx.c create rectangle %d %d %d %d -width %d -outline %s -fill #%2.2x%2.2x%2.2x -tags [list %lxBASE %lxALL]\n",
        glist_getcanvas(glist), xpos, ypos, xpos + x->x_w*x->x_zoom, ypos + x->x_h*x->x_zoom,
        x->x_zoom, x->x_sel ? "blue" : "black", x->x_color[0], x->x_color[1], x->x_color[2], x, x);
#endif
    pad_draw_io_let(x);
}

#ifdef PURR_DATA
static void pad_config(t_pad *x)
{
  char colorbuf[MAXPDSTRING];
  sprintf(colorbuf, "#%2.2x%2.2x%2.2x", x->x_color[0], x->x_color[1], x->x_color[2]);
  gui_vmess("gui_else_configure_pad", "xxsii", glist_getcanvas(x->x_glist), x, colorbuf, x->x_w, x->x_h);
}
#endif

static void pad_update(t_pad *x){
    if(glist_isvisible(x->x_glist) && gobj_shouldvis((t_gobj *)x, x->x_glist)){
#ifdef PURR_DATA
        pad_config(x);
#else
        int xpos = text_xpix(&x->x_obj, x->x_glist);
        int ypos = text_ypix(&x->x_obj, x->x_glist);
        sys_vgui(".x%lx.c coords %lxBASE %d %d %d %d\n",
                 glist_getcanvas(x->x_glist), x, xpos, ypos,
                 xpos + x->x_w*x->x_zoom, ypos + x->x_h*x->x_zoom);
#endif
        canvas_fixlinesfor(glist_getcanvas(x->x_glist), (t_text*)x);
    }
}

#ifdef PURR_DATA
static void pad_mouserelease(t_pad* x);

/* We hook into Purr Data's legacy mouse interface here so that we can get
   notified about mouse clicks (mouseup events, in particular, which the
   engine doesn't pass on). NOTE: The receiver that we bind these messages to
   is the proxy, not the main object, in order not to interfere with the
   object's own message processing. The proxy in turn processes the list
   messages from the interface in the pad_mouseclick method below.

   We really have to stretch Purr Data's poor old #legacy_mouseclick interface
   here to make it do things it was never designed to do. The interface is
   provided as a simple way to track the mouse on a toplevel canvas, nothing
   more. Thus in order to make this stuff work in the same way as in the
   vanilla/tcl version, we need to:

   (1) Keep track of drag actions (and thus of both mousedown and mouseup
   events, although we only report the latter). ELSE's pad reports a mouseup
   event anywhere on the canvas, as long as the click was initiated inside its
   rectangle.

   (2) If needed, translate the coordinates so that they are relative to pad
   objects in gop subpatches. This is necessary because #legacy_mouseclick
   only delivers coordinates relative to the toplevel canvas, and we need the
   coordinates relative to the target pad object in a gop subpatch to match
   the mousedown event against the click rectangle in order to make (1) work
   correctly.

   It really seems that we go to quite some lengths here in order to properly
   report a simple mouseup event, but there you go.

   One issue still remains due to #legacy_mouseclick not allowing us to filter
   events by canvas. You will notice this if you have pad objects in separate
   toplevel patches, in which case a single mouseup event may be reported by
   multiple objects in different patches if their rectangles happen to match
   up. This is a known bug which simply can't be avoided in the current
   implementation. */

// calculate click coords relative to a (sub)canvas
static void gop_translate_coords(t_pad *x, int *xp, int *yp)
{
  int xpos = *xp, ypos = *yp;
  // we are here:
  t_glist *gl = x->x_glist;
  // this is the toplevel canvas which xpos, ypos refer to:
  t_canvas *tl = glist_getcanvas(gl);
  //post("%p: xpos, ypos = %d %d", gl, xpos, ypos);
  while (gl != tl) {
    t_glist *owner = gl->gl_owner; // our parent, assert != NULL
    int isgop = gl->gl_isgraph; // gop?
    // if not a gop, then the click isn't for us, bail out
    if (!isgop) {
      xpos = ypos = -1;
      break;
    }
    // determine the position of the gop canvas in its parent
    int xp = text_xpix(&gl->gl_obj, owner);
    int yp = text_ypix(&gl->gl_obj, owner);
    // gop area
    int gop_x = gl->gl_xmargin;
    int gop_y = gl->gl_ymargin;
    // true position is offset by the gop offset minus xp, yp
    xpos += gop_x-xp; ypos += gop_y-yp;
    //post("%p (%d %d): gop = %d (%d %d) => xpos, ypos = %d %d", gl, xp, yp, isgop, gop_x, gop_y, xpos, ypos);
    // walk up the tree
    gl = owner;
  }
  *xp = xpos; *yp = ypos;
}

static void pad_mouseclick(t_edit_proxy *p, t_symbol *s, int argc,
			   t_atom *argv)
{
  t_pad *x = p->p_cnv;
  // 1st arg is the button state (0 = up, 1 = down)
  int state = (int)atom_getfloatarg(0, argc, argv);
  // 2nd arg is the button number
  int b = (int)atom_getfloatarg(1, argc, argv);
  // 3rd and 4th arg are the (toplevel) canvas coordinates
  int xpos = (int)atom_getfloatarg(2, argc, argv);
  int ypos = (int)atom_getfloatarg(3, argc, argv);
  // We shouldn't actually see the same mouse state being reported twice here,
  // but if we do, we bail out here.
  if (state == x->x_mousestate) return;
  x->x_mousestate = state;
  /* We only report mouseup events, and no right-clicks, which is what the
     Tcl/Tk version does. We also need to keep track of drag actions since in
     this case the mouseup will be reported *anywhere* on the canvas as long
     as the initial mousedown happened inside the pad rectangle. */
  if (b<3) {
    if (state) {
      gop_translate_coords(x, &xpos, &ypos);
      x->x_mousex = xpos; x->x_mousey = ypos;
    } else {
      t_class *c = pd_class((t_pd *)x);
      t_canvas *canvas=glist_getcanvas(x->x_glist);
      int x1,y1,x2,y2;
      c->c_wb->w_getrectfn((t_gobj *)x,canvas,&x1,&y1,&x2,&y2);
      //gop_translate_coords(x, &xpos, &ypos);
      //post("mouseup at %d %d (was %d %d), rect: %d %d %d %d, match: %d", xpos, ypos, x->x_mousex, x->x_mousey, x1,y1,x2,y2, x->x_mousex >= x1 && x->x_mousey >= y1 && x->x_mousex <= x2 && x->x_mousey <= y2);
      if (x->x_mousex >= x1 && x->x_mousey >= y1 && x->x_mousex <= x2 && x->x_mousey <= y2) {
	pad_mouserelease(x);
      }
    }
  }
}
#endif

static void pad_vis(t_gobj *z, t_glist *glist, int vis){
    t_pad* x = (t_pad*)z;
    t_canvas *cv = glist_getcanvas(glist);
    if(vis){
        pad_draw(x, glist);
#ifdef PURR_DATA
	// bind to #legacy_mouseclick
	if (!x->x_mousebind) {
	  pd_bind(&x->x_proxy->p_obj.ob_pd, x->x_mouseclick);
	  x->x_mousebind = 1;
	}
#else
        sys_vgui(".x%lx.c bind %lxBASE <ButtonRelease> {pdsend [concat %s _mouserelease \\;]}\n", cv, x, x->x_bindname->s_name);
#endif
    }
    else
        pad_erase(x, glist);
}

static void pad_delete(t_gobj *z, t_glist *glist){
    canvas_deletelinesfor(glist, (t_text *)z);
}

static void pad_displace(t_gobj *z, t_glist *glist, int dx, int dy){
    t_pad *x = (t_pad *)z;
    x->x_obj.te_xpix += dx, x->x_obj.te_ypix += dy;
    /* In Purr Data, all of this is taken care of automatically, we only need
       to fix the cord lines afterwards. */
#ifndef PURR_DATA
    sys_vgui(".x%lx.c move %lxALL %d %d\n", glist_getcanvas(glist), x, dx*x->x_zoom, dy*x->x_zoom);
#endif
    canvas_fixlinesfor(glist, (t_text*)x);
}

static void pad_select(t_gobj *z, t_glist *glist, int sel){
    t_pad *x = (t_pad *)z;
    t_canvas *cv = glist_getcanvas(glist);
#ifdef PURR_DATA
    /* Using existing gui_gobj functions from our api here. These just add or
       remove the "selected" class from the gobj. Rendering the rectangle
       border in the right color is then taken care of by the browser engine,
       using the active css style. So there are no hard-coded border colors as
       in the tcl code. */
    if((x->x_sel = sel))
        gui_vmess("gui_gobj_select", "xx", cv, x);
    else
        gui_vmess("gui_gobj_deselect", "xx", cv, x);
#else
    if((x->x_sel = sel))
        sys_vgui(".x%lx.c itemconfigure %lxBASE -outline blue\n", cv, x);
    else
        sys_vgui(".x%lx.c itemconfigure %lxBASE -outline black\n", cv, x);
#endif
}

static void pad_getrect(t_gobj *z, t_glist *glist, int *xp1, int *yp1, int *xp2, int *yp2){
    t_pad *x = (t_pad *)z;
    *xp1 = text_xpix(&x->x_obj, glist);
    *yp1 = text_ypix(&x->x_obj, glist);
    *xp2 = *xp1 + x->x_w*x->x_zoom;
    *yp2 = *yp1 + x->x_h*x->x_zoom;
}

static void pad_save(t_gobj *z, t_binbuf *b){
  t_pad *x = (t_pad *)z;
  binbuf_addv(b, "ssiisiiiii", gensym("#X"),gensym("obj"),
    (int)x->x_obj.te_xpix, (int)x->x_obj.te_ypix,
    atom_getsymbol(binbuf_getvec(x->x_obj.te_binbuf)),
    x->x_w, x->x_h, x->x_color[0], x->x_color[1], x->x_color[2]);
  binbuf_addv(b, ";");
}

static void pad_mouserelease(t_pad* x){
    if(!x->x_glist->gl_edit){ // ignore if toggle or edit mode!
        t_atom at[1];
        SETFLOAT(at, 0);
        outlet_anything(x->x_obj.ob_outlet, gensym("click"), 1, at);
    }
}

static void pad_motion(t_pad *x, t_floatarg dx, t_floatarg dy){
    x->x_x += (int)(dx/x->x_zoom);
    x->x_y -= (int)(dy/x->x_zoom);
    t_atom at[2];
    SETFLOAT(at, (t_float)x->x_x);
    SETFLOAT(at+1, (t_float)x->x_y);
    outlet_anything(x->x_obj.ob_outlet, &s_list, 2, at);
}

static int pad_click(t_gobj *z, struct _glist *glist, int xpix, int ypix,
int shift, int alt, int dbl, int doit){
    dbl = shift = alt = 0;
    t_pad* x = (t_pad *)z;
    t_atom at[3];
    int xpos = text_xpix(&x->x_obj, glist), ypos = text_ypix(&x->x_obj, glist);
    x->x_x = (xpix - xpos) / x->x_zoom;
    x->x_y = x->x_h - (ypix - ypos) / x->x_zoom;
    if(doit){
        SETFLOAT(at, (float)doit);
        outlet_anything(x->x_obj.ob_outlet, gensym("click"), 1, at);
        glist_grab(x->x_glist, &x->x_obj.te_g, (t_glistmotionfn)pad_motion, 0, (float)xpix, (float)ypix);
    }
    else{
        SETFLOAT(at, (float)x->x_x);
        SETFLOAT(at+1, (float)x->x_y);
        outlet_anything(x->x_obj.ob_outlet, &s_list, 2, at);
    }
    return(1);
}

static void pad_dim(t_pad *x, t_floatarg f1, t_floatarg f2){
    int w = f1 < 12 ? 12 : (int)f1, h = f2 < 12 ? 12 : (int)f2;
    if(w != x->x_w || h != x->x_h){
        x->x_w = w; x->x_h = h;
        pad_update(x);
    }
}

static void pad_width(t_pad *x, t_floatarg f){
    int w = f < 12 ? 12 : (int)f;
    if(w != x->x_w){
        x->x_w = w;
        pad_update(x);
    }
}

static void pad_height(t_pad *x, t_floatarg f){
    int h = f < 12 ? 12 : (int)f;
    if(h != x->x_h){
        x->x_h = h;
        pad_update(x);
    }
}

static void pad_color(t_pad *x, t_floatarg red, t_floatarg green, t_floatarg blue){
    int r = red < 0 ? 0 : red > 255 ? 255 : (int)red;
    int g = green < 0 ? 0 : green > 255 ? 255 : (int)green;
    int b = blue < 0 ? 0 : blue > 255 ? 255 : (int)blue;
    if((x->x_color[0] != r || x->x_color[1] != g || x->x_color[2] != b)){
        x->x_color[0] = r; x->x_color[1] = g; x->x_color[2] = b;
        if(glist_isvisible(x->x_glist) && gobj_shouldvis((t_gobj *)x, x->x_glist))
#ifdef PURR_DATA
            pad_config(x);
#else
            sys_vgui(".x%lx.c itemconfigure %lxBASE -fill #%2.2x%2.2x%2.2x\n",
            glist_getcanvas(x->x_glist), x, r, g, b);
#endif
    }
}

static void pad_zoom(t_pad *x, t_floatarg zoom){
    x->x_zoom = __zoom((int)zoom);
}

#ifdef PURR_DATA
static void edit_proxy_float(t_edit_proxy *p, t_float f)
{
    int edit = f;
    if(p->p_cnv->x_edit != edit){
      p->p_cnv->x_edit = edit;
      if(edit)
	pad_draw_io_let(p->p_cnv);
      else{
	pad_erase_io_let(p->p_cnv);
      }
    }
}
#endif

static void edit_proxy_any(t_edit_proxy *p, t_symbol *s, int ac, t_atom *av){
    int edit = ac = 0;
    if(p->p_cnv){
        if(s == gensym("editmode"))
            edit = (int)(av->a_w.w_float);
        else if(s == gensym("obj") || s == gensym("msg") || s == gensym("floatatom")
        || s == gensym("symbolatom") || s == gensym("text") || s == gensym("bng")
        || s == gensym("toggle") || s == gensym("numbox") || s == gensym("vslider")
        || s == gensym("hslider") || s == gensym("vradio") || s == gensym("hradio")
        || s == gensym("vumeter") || s == gensym("mycnv") || s == gensym("selectall")){
            edit = 1;
        }
        else
            return;
        if(p->p_cnv->x_edit != edit){
            p->p_cnv->x_edit = edit;
            if(edit)
                pad_draw_io_let(p->p_cnv);
            else{
                pad_erase_io_let(p->p_cnv);
            }
        }
    }
}

static void pad_free(t_pad *x){
    pd_unbind(&x->x_obj.ob_pd, x->x_bindname);
    x->x_proxy->p_cnv = NULL;
    clock_delay(x->x_proxy->p_clock, 0);
    gfxstub_deleteforkey(x);
}

static void edit_proxy_free(t_edit_proxy *p){
    pd_unbind(&p->p_obj.ob_pd, p->p_sym);
    clock_free(p->p_clock);
    pd_free(&p->p_obj.ob_pd);
}

static t_edit_proxy * edit_proxy_new(t_pad *x, t_symbol *s){
    t_edit_proxy *p = (t_edit_proxy*)pd_new(edit_proxy_class);
    p->p_cnv = x;
    pd_bind(&p->p_obj.ob_pd, p->p_sym = s);
    p->p_clock = clock_new(p, (t_method)edit_proxy_free);
    return(p);
}

static void *pad_new(t_symbol *s, int ac, t_atom *av){
    t_pad *x = (t_pad *)pd_new(pad_class);
    t_canvas *cv = canvas_getcurrent();
    x->x_glist = (t_glist*)cv;
    char buf[MAXPDSTRING];
#ifdef PURR_DATA
    snprintf(buf, MAXPDSTRING-1, __cvfs "#editmode", (unsigned long)cv);
    buf[MAXPDSTRING-1] = 0;
    x->x_proxy = edit_proxy_new(x, gensym(buf));
    // symbol to bind to receive legacy mouse messages
    x->x_mouseclick = gensym("#legacy_mouseclick");
    x->x_mousestate = x->x_mousex = x->x_mousey = -1;
    x->x_mousebind = 0;
#else
    snprintf(buf, MAXPDSTRING-1, __cvfs, (unsigned long)cv);
    buf[MAXPDSTRING-1] = 0;
    x->x_proxy = edit_proxy_new(x, gensym(buf));
#endif
    sprintf(buf, "#%lx", (long)x);
    pd_bind(&x->x_obj.ob_pd, x->x_bindname = gensym(buf));
    x->x_edit = cv->gl_edit;
    x->x_zoom = __zoom(x->x_glist->gl_zoom);
    x->x_x = x->x_y = 0;
    x->x_color[0] = x->x_color[1] = x->x_color[2] = 255;
    int w = 127, h = 127;
    if(ac && av->a_type == A_FLOAT){ // 1st Width
        w = av->a_w.w_float;
        ac--; av++;
        if(ac && av->a_type == A_FLOAT){ // 2nd Height
            h = av->a_w.w_float;
            ac--, av++;
            if(ac && av->a_type == A_FLOAT){ // Red
                x->x_color[0] = (unsigned char)av->a_w.w_float;
                ac--, av++;
                if(ac && av->a_type == A_FLOAT){ // Green
                    x->x_color[1] = (unsigned char)av->a_w.w_float;
                    ac--, av++;
                    if(ac && av->a_type == A_FLOAT){ // Blue
                        x->x_color[2] = (unsigned char)av->a_w.w_float;
                        ac--, av++;
                    }
                }
            }
        }
    }
    while(ac > 0){
        if(av->a_type == A_SYMBOL){
            s = atom_getsymbolarg(0, ac, av);
            if(s == gensym("-dim")){
                if(ac >= 3 && (av+1)->a_type == A_FLOAT && (av+2)->a_type == A_FLOAT){
                    w = atom_getfloatarg(1, ac, av);
                    h = atom_getfloatarg(2, ac, av);
                    ac-=3, av+=3;
                }
                else goto errstate;
            }
            else if(s == gensym("-color")){
                if(ac >= 4 && (av+1)->a_type == A_FLOAT
                   && (av+2)->a_type == A_FLOAT
                   && (av+3)->a_type == A_FLOAT){
                    int r = (int)atom_getfloatarg(1, ac, av);
                    int g = (int)atom_getfloatarg(2, ac, av);
                    int b = (int)atom_getfloatarg(3, ac, av);
                    x->x_color[0] = r < 0 ? 0 : r > 255 ? 255 : r;
                    x->x_color[1] = g < 0 ? 0 : g > 255 ? 255 : g;
                    x->x_color[2] = b < 0 ? 0 : b > 255 ? 255 : b;
                    ac-=4, av+=4;
                }
                else goto errstate;
            }
            else goto errstate;
        }
        else goto errstate;
    }
    x->x_w = w, x->x_h = h;
    outlet_new(&x->x_obj, &s_anything);
    return(x);
    errstate:
        pd_error(x, "[pad]: improper args");
        return(NULL);
}

void pad_setup(void){
    pad_class = class_new(gensym("pad"), (t_newmethod)pad_new,
        (t_method)pad_free, sizeof(t_pad), 0, A_GIMME, 0);
    class_addmethod(pad_class, (t_method)pad_dim, gensym("dim"), A_FLOAT, A_FLOAT, 0);
    class_addmethod(pad_class, (t_method)pad_width, gensym("width"), A_FLOAT, 0);
    class_addmethod(pad_class, (t_method)pad_height, gensym("height"), A_FLOAT, 0);
    class_addmethod(pad_class, (t_method)pad_color, gensym("color"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(pad_class, (t_method)pad_zoom, gensym("zoom"), A_CANT, 0);
    class_addmethod(pad_class, (t_method)pad_mouserelease, gensym("_mouserelease"), 0);
    edit_proxy_class = class_new(0, 0, 0, sizeof(t_edit_proxy), CLASS_NOINLET | CLASS_PD, 0);
#ifdef PURR_DATA
    // catch list messages delivered by the legacy mouse interface
    class_addlist(edit_proxy_class, pad_mouseclick);
    // catch edit mode state message delivered to the #editmode receiver
    class_addfloat(edit_proxy_class, edit_proxy_float);
#else
    class_addanything(edit_proxy_class, edit_proxy_any);
#endif
    pad_widgetbehavior.w_getrectfn  = pad_getrect;
    pad_widgetbehavior.w_displacefn = pad_displace;
    pad_widgetbehavior.w_selectfn   = pad_select;
    pad_widgetbehavior.w_activatefn = NULL;
    pad_widgetbehavior.w_deletefn   = pad_delete;
    pad_widgetbehavior.w_visfn      = pad_vis;
    pad_widgetbehavior.w_clickfn    = pad_click;
    class_setsavefn(pad_class, pad_save);
    class_setwidget(pad_class, &pad_widgetbehavior);
}
