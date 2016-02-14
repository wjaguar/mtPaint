/*	vcode.h
	Copyright (C) 2013-2016 Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

enum {
	op_WEND = 0,
	op_WSHOW,
	op_WDIALOG,
	op_uWEND,
	op_uWSHOW,

	op_END_LAST,
	op_WDONE = op_END_LAST,

	op_MAINWINDOW,
	op_WINDOW,
	op_WINDOWm,
	op_DIALOGm,
	op_FPICKpm,
	op_POPUP,
	op_TOPVBOX,
	op_TOPVBOXV,
	op_uWINDOW,
	op_uFPICK,
	op_uTOPBOX,

	op_DOCK,
	op_HVSPLIT,
	op_PAGE,
	op_PAGEi,
	op_TABLE,
	op_VBOX,
	op_HBOX,
	op_EQBOX,
	op_FRAME,
	op_EFRAME,
	op_SCROLL,
	op_NBOOK,
	op_NBOOKl,
	op_PLAINBOOK,
	op_BOOKBTN,
	op_STATUSBAR,
	op_STLABEL,
	op_HSEP,
	op_LABEL,
	op_WLABEL,
	op_HLABEL,
	op_HLABELm,
	op_TLTEXT,
	op_PROGRESS,
	op_COLORPATCH,
	op_RGBIMAGE,
	op_RGBIMAGEP,
	op_CANVASIMG,
	op_CANVASIMGB,
	op_FCIMAGEP,
	op_NOSPIN,
	op_uOP,
	op_uFRAME,
	op_uLABEL,

	op_CSCROLL,
	op_CANVAS,

	op_SPIN,
	op_SPINc,
	op_FSPIN,
	op_SPINa,

	op_TLSPINPACK,
	op_SPINSLIDE,
	op_SPINSLIDEa,
	op_CHECK,
	op_CHECKb,
	op_RPACK,
	op_RPACKD,
	op_OPT,
	op_OPTD,
	op_COMBO,
	op_ENTRY,
	op_MLENTRY,
	op_PENTRY,
	op_PATH,
	op_PATHs,
	op_TEXT,
	op_COMBOENTRY,
	op_FONTSEL,
	op_HEXENTRY,
	op_EYEDROPPER,
	op_KEYBUTTON,
	op_TABLETBTN,
	op_COLOR,
	op_TCOLOR,
	op_COLORLIST,
	op_COLORLISTN,
	op_GRADBAR,
	op_PCTCOMBO,
	op_LISTCCr,
	op_LISTC,
	op_LISTCd,
	op_LISTCu,
	op_LISTCS,
	op_LISTCX,
	op_OKBTN,
	op_CANCELBTN,
	op_DONEBTN,
	op_TOGGLE,
	op_BUTTON,
	op_TOOLBAR,
	op_SMARTTBAR,
	op_SMARTTBMORE,
	op_TBBUTTON,
	op_TBTOGGLE,
	op_TBRBUTTON,
	op_TBBOXTOG,
	op_TBSPACE,
	op_TWOBOX,

	op_MENUBAR,
	op_SMARTMENU,
	op_SMDONE,
	op_SUBMENU,
	op_ESUBMENU,
	op_SSUBMENU,

	op_MENU_0,
	op_MENUITEM = op_MENU_0,
	op_MENUCHECK,
	op_MENURITEM,

	op_MENU_LAST,
	op_MENUTEAR = op_MENU_LAST,
	op_MENUSEP,

	op_MOUNT,
	op_REMOUNT,
	op_uCHECK,
	op_uCHECKb,
	op_uSPIN,
	op_uFSPIN,
	op_uSPINa,
	op_uSCALE,
	op_uOPT,
	op_uOPTD,
	op_uRPACK,
	op_uRPACKD,
	op_uENTRY,
	op_uCOLOR,
	op_uLISTCC,
	op_uLISTC,
	op_uOKBTN,
	op_uBUTTON,
	op_uMENUBAR,
	
	op_uMENU_0,
	op_uMENUITEM = op_uMENU_0,
	op_uMENUCHECK,
	op_uMENURITEM,

	op_uMENU_LAST,
	op_uMOUNT = op_uMENU_LAST,

	op_CLIPBOARD,

	op_XBMCURSOR,
	op_SYSCURSOR,

	op_CTL_0, // control tags - same for all modes
	op_WLIST = op_CTL_0,
	op_COLUMNDATA,

	op_COLUMN_0,
	op_IDXCOLUMN = op_COLUMN_0,
	op_TXTCOLUMN,
	op_XTXTCOLUMN,
	op_FILECOLUMN,
	op_CHKCOLUMN,

	op_COLUMN_LAST,

	op_EVT_0 = op_COLUMN_LAST,
	op_EVT_OK = op_EVT_0,
	op_EVT_CANCEL,
	op_EVT_CLICK,
	op_EVT_SELECT,
	op_EVT_CHANGE,
	op_EVT_DESTROY, // before deallocation
	op_EVT_SCRIPT, // by cmd_setstr()
	op_EVT_MULTI, // by same, with intlist data & ret
	op_EVT_KEY, // with key data & ret
	op_EVT_MOUSE, // with button data & ret
	op_EVT_MMOUSE, // movement, with same
	op_EVT_RMOUSE, // release, with same
	op_EVT_XMOUSE0,
	op_EVT_XMOUSE = op_EVT_XMOUSE0, // with button & pressure data & ret
	op_EVT_MXMOUSE, // movement, with same
	op_EVT_RXMOUSE, // release, with same
	op_EVT_CROSS, // enter/leave, with flag
	op_EVT_SCROLL, // with 2 directions
	op_EVT_EXT, // generic event with extra data
	op_EVT_DRAGFROM, // with drag data & ret
	op_EVT_DROP, // with drag data 
	op_EVT_COPY, // with copy data
	op_EVT_PASTE, // with copy data & ret

	op_EVT_LAST,
	op_TRIGGER = op_EVT_LAST,

	op_EV_0, // dynamic tags - event values
	op_EV_MOUSE = op_EV_0,
	op_EV_DRAGFROM,
	op_EV_COPY,

	op_EV_LAST,

//	op_EXEC,
	op_GOTO,
	op_CALL,
	op_RET,
	op_IF,
	op_UNLESS,
	op_UNLESSbt,
	op_ENDIF,
	op_REF,
	op_CLEANUP,
	op_TALLOC,
	op_TCOPY,
	op_IDENT,
	op_uOPNAME,
	op_uALTNAME,

	op_CTL_LAST,

	op_CLIPFORM = op_CTL_LAST,
	op_DRAGDROP,
	op_ACTMAP,
	op_KEYMAP,
	op_SHORTCUT,
	op_WANTKEYS,
	op_MKSHRINK,
	op_NORESIZE,
	op_WANTMAX,
	op_KEEPWIDTH,
	op_WXYWH,
	op_WPMOUSE,
	op_WPWHEREVER,
	op_HIDDEN,
	op_INSENS,
	op_FOCUS,
	op_WIDTH,
	op_HEIGHT,
	op_ONTOP,
	op_RAISED,

	op_BOR_0,
	op_BOR_TABLE = op_BOR_0,
	op_BOR_NBOOK,
	op_BOR_SCROLL,
	op_BOR_SPIN,
	op_BOR_SPINSLIDE,
	op_BOR_LABEL,
	op_BOR_OPT,
	op_BOR_BUTTON,
	op_BOR_TOOLBAR,
	op_BOR_POPUP,
	op_BOR_TOPVBOX,
	op_BOR_CHECK,
	op_BOR_FRAME,
	op_BOR_RPACK,
	op_BOR_ENTRY,
	op_BOR_LISTCC,

	op_BOR_LAST,
	op_LAST = op_BOR_LAST
};

//	Function to run with EXEC
//typedef void **(*ext_fn)(void **r, GtkWidget ***wpp, void **wdata);
//	Function to run with MOUNT
typedef void **(*mnt_fn)(void **wdata);

//	Structure which COLORLIST provides to EXT event
typedef struct {
	int idx;	// which row was clicked
	int button;	// which button was clicked (1-3 etc)
} colorlist_ext;

//	Structure which is provided to KEY event
typedef struct {
	unsigned int key;	// keysym
	unsigned int lowkey;	// lowercase keysym
	unsigned int realkey;	// keycode
	unsigned int state;	// modifier flags
} key_ext;

#define MAX_PRESSURE 0xFF00 /* Nicest fixedpoint scale */

//	Structure which is provided to MOUSE/MMOUSE/RMOUSE event
typedef struct {
	int x, y;
	int button;	// what was pressed, stays pressed, or got released
	int count;	// 1/2/3 single/double/triple click, 0 move, -1 release
	unsigned int state;	// button & modifier flags
	int pressure;	// scaled to 0..MAX_PRESSURE
} mouse_ext;

//	Structure which is provided to SCROLL event
typedef struct {
	int xscroll, yscroll;
	unsigned int state;
} scroll_ext;

//	Structure which is supplied to CLIPFORM
typedef struct {
	char *target;	// MIME type
	void *id;	// anything
	int size;	// fixed, or 0 for variable
	int format;	// bit width: 8/16/32 (0 defaults to 8)
} clipform_dd;

//	Structure which is provided to DRAGFROM and DROP event
typedef struct {
	int x, y;
	clipform_dd *format;
	void *data;
	int len;
} drag_ext;

//	Structure which is provided to COPY and PASTE event
typedef struct {
	clipform_dd *format;
	void *data;
	int len;
} copy_ext;

//	Structure which is provided by KEYMAP
typedef struct {
	char *name;
	int slot;
} key_dd;

typedef struct {
	int nslots, nkeys, maxkeys;
	key_dd *keys;
	char **slotnames;
} keymap_dd;

//	Structure which is provided to MULTI event
typedef struct {
	int nrows, ncols;
	int mincols; // Minimum tuple size
	int fractcol; // Which column is fixedpoint (-1 if all integer)
	int *rows[1]; // Pointers to rows (int arrays preceded by their length)
} multi_ext;

//	Structure which is supplied to WINDOW_TEXTENG
typedef struct {
	char *text, *font;
	int angle, dpi;
	rgbcontext ctx;
} texteng_dd;

//	Values of mouse count
enum {
	MCOUNT_RELEASE	= -1,
	MCOUNT_MOVE	= 0,
	MCOUNT_CLICK	= 1,
	MCOUNT_2CLICK	= 2,
	MCOUNT_3CLICK	= 3,
	/* Used to denote other events */
	MCOUNT_ENTER	= 10,
	MCOUNT_LEAVE,
	MCOUNT_NOTHING
};

#define _0mask (0)
#define _Cmask (GDK_CONTROL_MASK)
#define _Smask (GDK_SHIFT_MASK)
#define _Amask (GDK_MOD1_MASK)
#define _CSmask (GDK_CONTROL_MASK | GDK_SHIFT_MASK)
#define _CAmask (GDK_CONTROL_MASK | GDK_MOD1_MASK)
#define _SAmask (GDK_SHIFT_MASK | GDK_MOD1_MASK)
#define _CSAmask (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)
#define _B1mask (GDK_BUTTON1_MASK)
#define _B2mask (GDK_BUTTON2_MASK)
#define _B3mask (GDK_BUTTON3_MASK)
#define _B13mask (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)

//	Functions to run with EVENT
typedef void (*evt_fn)(void *ddata, void **wdata, int what, void **where);
typedef void (*evtx_fn)(void *ddata, void **wdata, int what, void **where,
	void *xdata);
typedef int (*evtxr_fn)(void *ddata, void **wdata, int what, void **where,
	void *xdata);

//	Textengine flags
int texteng_aa;  /* Antialias */
int texteng_rot; /* Rotate */
int texteng_dpi; /* Set DPI */
int texteng_con; /* Work in console mode */

//	Build a dialog window out of V-code decription
//void **run_create(const void **ifcode, const void *ddata, int ddsize);
void **run_create_(void **ifcode, void *ddata, int ddsize, char **script);
#define run_create(A,B,C) run_create_(A, B, C, NULL)
//	Query dialog contents using its widget-map
void run_query(void **wdata);
//	Destroy a dialog by its widget-map
void run_destroy(void **wdata);

#define VSLOT_SIZE 3

//	Extract data structure out of widget-map
#define GET_DDATA(V) ((V)[0])
//	Extract toplevel window slot out of widget-map
#define GET_WINDOW(V) ((V) + VSLOT_SIZE)
//	Extract actual toplevel window out of widget-map
#define GET_REAL_WINDOW(V) ((V)[VSLOT_SIZE + 0])
//	Iterate over slots
#define NEXT_SLOT(V) ((V) + VSLOT_SIZE)
#define PREV_SLOT(V) ((V) - VSLOT_SIZE)
#define SLOT_N(V,N) ((V) + (N) * VSLOT_SIZE)
//	Extract ID out of toolbar item
#define TOOL_ID(V) (int)(((void **)(V)[1])[2])
#define TOOL_IR(V) (int)(((void **)(V)[1])[5])
//	Combine action and mode
#define ACTMOD(A,M) (((A) << 16) | (((M) + 0x8000) & 0xFFFF))

//	From widget to its wdata
void **get_wdata(GtkWidget *widget, char *id);

//	Run event handler, defaulting to run_destroy()
void do_evt_1_d(void **slot);

//	Search modes
#define MLEVEL_FLAT  (-1) /* All */
#define MLEVEL_GROUP (-2) /* Groups */
#define MLEVEL_BLOCK (-3) /* In group */

//	Find slot by text name and menu level
void **find_slot(void **slot, char *id, int l, int mlevel);

//	From slot to its wdata
void **wdata_slot(void **slot);
//	From event to its originator
void **origin_slot(void **slot);

//	From slot to its storage location (its own, *not* originator's)
void *slot_data(void **slot, void *ddata);

//	Raise event on slot
void cmd_event(void **slot, int op);

//	Set sensitive state on slot
void cmd_sensitive(void **slot, int state);
//	Set visible state on slot
void cmd_showhide(void **slot, int state);
//	Set value on slot
void cmd_set(void **slot, int v);
//	Set text-encoded value on slot (-1 fail, 0 unused, 1 set)
int cmd_setstr(void **slot, char *s);
//	Read back slot value (as is), return its storage location
void *cmd_read(void **slot, void *ddata);
//	Read extra data from slot
void cmd_peekv(void **slot, void *res, int size, int idx);
//	Set extra data on slot
void cmd_setv(void **slot, void *res, int idx);
//	Repaint slot
void cmd_repaint(void **slot);
//	Reset slot (or a group)
void cmd_reset(void **slot, void *ddata);
//	Set cursor for slot window
void cmd_cursor(void **slot, void **cursor);
//	Check extra state on slot
int cmd_checkv(void **slot, int idx);
//	Run script on slot
int cmd_run_script(void **slot, char **strs);

///	Handler for dialog buttons
void dialog_event(void *ddata, void **wdata, int what, void **where);

enum {
	pk_NONE = 0,
	pk_PACK,
	pk_XPACK,
	pk_DEF = pk_XPACK,
	pk_EPACK,
	pk_PACKEND,
	pk_TABLE1x,
	pk_TABLE,
	pk_TABLEx,

	pk_LAST
};

enum {
	col_DIRECT = 0,
	col_PTR,
	col_REL
};
#define COL_LSHIFT 24

#define WB_OPBITS      12
#define WB_OPMASK  0x0FFF /* ((1 << WB_OPBITS) - 1) */
#define WB_PKBITS       4
#define WB_PKMASK     0xF /* ((1 << WB_PKBITS) - 1) */
#define WB_GETPK(X) (((X) >> WB_OPBITS) & WB_PKMASK)

#define WB_SFLAG  0x100000 /* Scriptable */
#define WB_FFLAG  0x080000
#define WB_NFLAG  0x040000
#define WB_REF1   0x010000
#define WB_REF2   0x020000
#define WB_REF3   0x030000
#define WB_GETREF(X) (((X) >> (WB_OPBITS + WB_PKBITS)) & 3)
#define WB_LSHIFT     24
#define WB_GETLEN(X) ((X) >> WB_LSHIFT)

#define WBlen(L) ((L) << WB_LSHIFT)
#define WBpk(P) ((P) << WB_OPBITS)
#define WB_0	0
#define WB_F	WB_FFLAG
#define WB_S	WB_SFLAG
#define WB_R	WB_REF1
#define WB_RF	(WB_REF1 + WB_FFLAG)
#define WB_RS	(WB_REF1 + WB_SFLAG)
#define WB_R2	WB_REF2
#define WB_R2F	(WB_REF2 + WB_FFLAG)
#define WB_R2S	(WB_REF2 + WB_SFLAG)
#define WB_R3	WB_REF3
#define WB_R3F	(WB_REF3 + WB_FFLAG)
#define WB_N	WB_NFLAG
#define WB_NF	(WB_NFLAG + WB_FFLAG)
#define WB_RNF	(WB_REF1 + WB_NFLAG + WB_FFLAG)
#define WB_R2NF	(WB_REF2 + WB_NFLAG + WB_FFLAG)
#define WBp_(OP,L,P,F) (void *)(OP + WBlen(L) + WBpk(pk_##P) + WB_##F)

#define WBh(NM,L) WBp_(op_##NM, L, NONE, 0)
#define WBh_(NM,L) WBp_(op_##NM, L, PACK, 0)
#define WBh_x(NM,L) WBp_(op_##NM, L, XPACK, 0)
#define WBh_e(NM,L) WBp_(op_##NM, L, PACKEND, 0)
#define WBh_t(NM,L) WBp_(op_##NM, L, TABLE, 0)
#define WBh_tx(NM,L) WBp_(op_##NM, L, TABLEx, 0)
#define WBh_t1(NM,L) WBp_(op_##NM, L, TABLE1x, 0)
#define WBhf(NM,L) WBp_(op_##NM, L, NONE, F)
#define WBhf_(NM,L) WBp_(op_##NM, L, PACK, F)
#define WBhf_t(NM,L) WBp_(op_##NM, L, TABLE, F)
#define WBhs(NM,L) WBp_(op_##NM, L, NONE, S)
#define WBrh(NM,L) WBp_(op_##NM, L, NONE, R)
#define WBrh_(NM,L) WBp_(op_##NM, L, PACK, R)
#define WBrh_x(NM,L) WBp_(op_##NM, L, XPACK, R)
#define WBrh_c(NM,L) WBp_(op_##NM, L, EPACK, R)
#define WBrh_e(NM,L) WBp_(op_##NM, L, PACKEND, R)
#define WBrh_t(NM,L) WBp_(op_##NM, L, TABLE, R)
#define WBrh_tx(NM,L) WBp_(op_##NM, L, TABLEx, R)
#define WBrh_t1(NM,L) WBp_(op_##NM, L, TABLE1x, R)
#define WBrhf(NM,L) WBp_(op_##NM, L, NONE, RF)
#define WBrhf_(NM,L) WBp_(op_##NM, L, PACK, RF)
#define WBrhf_x(NM,L) WBp_(op_##NM, L, XPACK, RF)
#define WBrhf_t(NM,L) WBp_(op_##NM, L, TABLE, RF)
#define WBrhf_tx(NM,L) WBp_(op_##NM, L, TABLEx, RF)
#define WBrhf_t1(NM,L) WBp_(op_##NM, L, TABLE1x, RF)
#define WBrhs(NM,L) WBp_(op_##NM, L, NONE, RS)
#define WBrhs_(NM,L) WBp_(op_##NM, L, PACK, RS)
#define WBr2h(NM,L) WBp_(op_##NM, L, NONE, R2)
#define WBr2h_(NM,L) WBp_(op_##NM, L, PACK, R2)
#define WBr2h_x(NM,L) WBp_(op_##NM, L, XPACK, R2)
#define WBr2h_e(NM,L) WBp_(op_##NM, L, PACKEND, R2)
#define WBr2h_t(NM,L) WBp_(op_##NM, L, TABLE, R2)
#define WBr2hf_(NM,L) WBp_(op_##NM, L, PACK, R2F)
#define WBr2hf_x(NM,L) WBp_(op_##NM, L, XPACK, R2F)
#define WBr2hf_t(NM,L) WBp_(op_##NM, L, TABLE, R2F)
#define WBr2hs_(NM,L) WBp_(op_##NM, L, PACK, R2S)
#define WBr2hs_x(NM,L) WBp_(op_##NM, L, XPACK, R2S)
#define WBr2hs_t(NM,L) WBp_(op_##NM, L, TABLE, R2S)
#define WBr3h_(NM,L) WBp_(op_##NM, L, PACK, R3)
#define WBr3h_e(NM,L) WBp_(op_##NM, L, PACKEND, R3)
#define WBr3hf(NM,L) WBp_(op_##NM, L, NONE, R3F)
#define WBr3hf_(NM,L) WBp_(op_##NM, L, PACK, R3F)
#define WBhn(NM,L) WBp_(op_##NM, L, NONE, N)
#define WBhnf(NM,L) WBp_(op_##NM, L, NONE, NF)
#define WBhnf_(NM,L) WBp_(op_##NM, L, PACK, NF)
#define WBhnf_x(NM,L) WBp_(op_##NM, L, XPACK, NF)
#define WBhnf_t(NM,L) WBp_(op_##NM, L, TABLE, NF)
#define WBrhnf(NM,L) WBp_(op_##NM, L, NONE, RNF)
#define WBrhnf_(NM,L) WBp_(op_##NM, L, PACK, RNF)
#define WBrhnf_t(NM,L) WBp_(op_##NM, L, TABLE, RNF)
#define WBr2hnf_x(NM,L) WBp_(op_##NM, L, XPACK, R2NF)

#define WBfield(V) (void *)offsetof(WBbase, V)
#define WBwh(W,H) (void *)((H) + ((W) << 16))
#define WBxyl(X,Y,L) (void *)((Y) + ((X) << 8) + ((L - 1) << 16))
#define WBnh(N,H) (void *)(((H) & 255) + ((N) << 8))
#define WBbs(B,S) (void *)(((S) & 255) + ((B) << 8))
#define WBpbs(P,B,S) (void *)(((S) & 255) + ((B) << 8) + ((P) << 16))
#define WBppa(PX,PY,AX) (void *)((PY) + ((PX) << 8) + (AX << 16))

#define WEND WBh(WEND, 0)
#define WSHOW WBh(WSHOW, 0)
#define WDIALOG(V) WBhf(WDIALOG, 1), WBfield(V)
#define WDONE WBh(WDONE, 0)
#define MAINWINDOW(NM,ICN,W,H) WBrh(MAINWINDOW, 3), (NM), (ICN), WBwh(W, H)
#define WINDOW(NM) WBrh(WINDOW, 1), (NM)
#define WINDOWp(NP) WBrhnf(WINDOW, 1), WBfield(NP)
#define WINDOWm(NM) WBrh(WINDOWm, 1), (NM)
#define WINDOWpm(NP) WBrhnf(WINDOWm, 1), WBfield(NP)
#define DIALOGpm(NP) WBrhnf(DIALOGm, 1), WBfield(NP)
/* !!! Note: string "V" is in system encoding */
/* !!! This block holds 2 nested EVENT blocks */
#define FPICKpm(NP,F,V,HOK,HC) WBr3hf(FPICKpm, 3 + 2 * 2), WBfield(V), \
	WBfield(NP), WBfield(F), EVENT(OK, HOK), EVENT(CANCEL, HC)
#define POPUP(NM) WBrh(POPUP, 1), (NM)
#define TOPVBOX WBrh(TOPVBOX, 0)
#define TOPVBOXV WBrh(TOPVBOXV, 0)
#define DOCK(K) WBrh_(DOCK, 1), (K)
#define HVSPLIT WBrh_x(HVSPLIT, 0)
#define PAGE(NM) WBh(PAGE, 1), (NM)
#define PAGEvp(NP) WBhn(PAGE, 1), &(NP)
#define PAGEi(ICN,S) WBh(PAGEi, 2), (ICN), (void *)(S)
#define PAGEir(ICN,S) WBrh(PAGEi, 2), (ICN), (void *)(S)
#define TABLE(W,H) WBh_(TABLE, 1), WBwh(W, H)
#define TABLE2(H) TABLE(2, (H))
#define TABLEs(W,H,S) WBh_(TABLE, 2), WBwh(W, H), (void *)(S)
#define TABLEr(W,H) WBrh_(TABLE, 1), WBwh(W, H)
#define XTABLE(W,H) WBh_x(TABLE, 1), WBwh(W, H)
#define ETABLE(W,H) WBh_e(TABLE, 1), WBwh(W, H)
#define FTABLE(NM,W,H) FRAME(NM), TABLE(W, H)
#define VBOX WBh_(VBOX, 0)
#define VBOXr WBrh_(VBOX, 0)
#define VBOXbp(S,B,P) WBh_(VBOX, 1), WBpbs(P, B, S)
#define VBOXP VBOXbp(0, 0, 5)
#define VBOXB VBOXbp(0, 5, 0)
#define VBOXS VBOXbp(5, 0, 0)
#define VBOXPS VBOXbp(5, 0, 5)
#define VBOXBS VBOXbp(5, 5, 0)
#define VBOXPBS VBOXbp(5, 5, 5)
#define XVBOX WBh_x(VBOX, 0)
#define XVBOXbp(S,B,P) WBh_x(VBOX, 1), WBpbs(P, B, S)
#define XVBOXP XVBOXbp(0, 0, 5)
#define XVBOXB XVBOXbp(0, 5, 0)
#define XVBOXBS XVBOXbp(5, 5, 0)
#define EVBOX WBh_e(VBOX, 0)
#define HBOX WBh_(HBOX, 0)
#define HBOXbp(S,B,P) WBh_(HBOX, 1), WBpbs(P, B, S)
#define HBOXP HBOXbp(0, 0, 5)
#define HBOXPr WBrh_(HBOX, 1), WBpbs(5, 0, 0)
#define HBOXB HBOXbp(0, 5, 0)
#define XHBOX WBh_x(HBOX, 0)
#define XHBOXbp(S,B,P) WBh_x(HBOX, 1), WBpbs(P, B, S)
#define XHBOXP XHBOXbp(0, 0, 5)
#define XHBOXS XHBOXbp(5, 0, 0)
#define XHBOXBS XHBOXbp(5, 5, 0)
#define TLHBOXl(X,Y,L) WBh_t(HBOX, 1), WBxyl(X, Y, L)
#define FVBOX(NM) FRAME(NM), VBOX
#define FVBOXB(NM) FRAME(NM), VBOXB
#define FVBOXBS(NM) FRAME(NM), VBOXBS
#define FXVBOX(NM) XFRAME(NM), VBOX
#define FXVBOXB(NM) XFRAME(NM), VBOXB
#define EFVBOX EFRAME, VBOXbp(0, 10, 0)
#define FHBOXB(NM) FRAME(NM), HBOXB
#define EQBOX WBh_(EQBOX, 0)
#define EQBOXbp(S,B,P) WBh_(EQBOX, 1), WBpbs(P, B, S)
#define EQBOXP EQBOXbp(0, 0, 5)
#define EQBOXB EQBOXbp(0, 5, 0)
#define EQBOXS EQBOXbp(5, 0, 0)
#define EEQBOX WBh_e(EQBOX, 0)
#define FRAME(NM) WBh_(FRAME, 1), (NM)
#define XFRAME(NM) WBh_x(FRAME, 1), (NM)
#define XFRAMEp(V) WBhnf_x(FRAME, 1), WBfield(V)
#define EFRAME WBh_(EFRAME, 0)
#define SCROLL(HP,VP) WBh_(SCROLL, 1), WBnh(VP, HP)
#define XSCROLL(HP,VP) WBh_x(SCROLL, 1), WBnh(VP, HP)
#define FSCROLL(HP,VP) XFRAME(NULL), SCROLL(HP, VP)
#define CSCROLLv(A) WBrh_x(CSCROLL, 1), (A)
#define NBOOK WBh_x(NBOOK, 0)
#define NBOOKr WBrh_x(NBOOK, 0)
#define NBOOKl WBh_x(NBOOKl, 0)
#define PLAINBOOK WBrh_(PLAINBOOK, 0)
#define PLAINBOOKn(N) WBrh_(PLAINBOOK, 1), (void *)(N)
#define BOOKBTN(NM,V) WBhf_(BOOKBTN, 2), WBfield(V), (NM)
#define STATUSBAR WBrh_e(STATUSBAR, 0)
#define STLABEL(W,A) WBrh_(STLABEL, 1), (void *)((W) + ((A) << 16))
#define STLABELe(W,A) WBrh_e(STLABEL, 1), (void *)((W) + ((A) << 16))
#define HSEP WBh_(HSEP, 0)
#define HSEPl(V) WBh_(HSEP, 1), (void *)(V)
#define HSEPt WBh_(HSEP, 1), (void *)(-1)
#define MLABEL(NM) WBh_(LABEL, 1), (NM)
#define MLABELr(NM) WBrh_(LABEL, 1), (NM)
#define MLABELc(NM) WBh_(LABEL, 2), (NM), WBppa(0, 0, 5)
#define MLABELxr(NM,PX,PY,AX) WBrh_(LABEL, 2), (NM), WBppa(PX, PY, AX)
#define MLABELcp(V) WBhnf_(LABEL, 2), WBfield(V), WBppa(0, 0, 5)
#define WLABELp(V) WBhnf_x(WLABEL, 2), WBfield(V), WBppa(0, 0, 5)
#define XLABELcr(NM) WBrh_x(LABEL, 2), (NM), WBppa(0, 0, 5)
#define TLABEL(NM) WBh_t1(LABEL, 1), (NM)
#define TLABELr(NM) WBrh_t1(LABEL, 1), (NM)
#define TLABELx(NM,PX,PY,AX) WBh_t1(LABEL, 2), (NM), WBppa(PX, PY, AX)
#define TLLABELl(NM,X,Y,L) WBh_t(LABEL, 2), (NM), WBxyl(X, Y, L)
#define TLLABEL(NM,X,Y) TLLABELl(NM, X, Y, 1)
#define TLLABELx(NM,X,Y,PX,PY,AX) WBh_t(LABEL, 3), (NM), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TLLABELxr(NM,X,Y,PX,PY,AX) WBrh_t(LABEL, 3), (NM), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TLLABELp(V,X,Y) WBhnf_t(LABEL, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLLABELpx(V,X,Y,PX,PY,AX) WBhnf_t(LABEL, 3), WBfield(V), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TXLABEL(NM,X,Y) WBh_tx(LABEL, 3), (NM), WBppa(0, 0, 3), WBxyl(X, Y, 1)
#define HLABELp(V) WBhnf_(HLABEL, 1), WBfield(V)
#define HLABELmp(V) WBhnf_(HLABELm, 1), WBfield(V)
#define TLTEXT(S,X,Y) WBh_t(TLTEXT, 2), (S), WBxyl(X, Y, 1)
#define TLTEXTf(C,X,Y) WBhf_t(TLTEXT, 2), WBfield(C), WBxyl(X, Y, 1)
#define TLTEXTp(V,X,Y) WBhnf_t(TLTEXT, 2), WBfield(V), WBxyl(X, Y, 1)
#define PROGRESSp(V) WBrhnf_(PROGRESS, 1), WBfield(V)
#define COLORPATCHv(CP,W,H) WBrh_x(COLORPATCH, 2), (CP), WBwh(W, H)
#define RGBIMAGE(CP,A) WBrhnf_(RGBIMAGE, 2), WBfield(CP), WBfield(A)
#define TLRGBIMAGE(CP,A,X,Y) WBrhnf_t(RGBIMAGE, 3), WBfield(CP), WBfield(A), \
	WBxyl(X, Y, 1)
#define RGBIMAGEP(CC,W,H) WBrhf_(RGBIMAGEP, 2), WBfield(CC), WBwh(W, H)
#define CANVASIMGv(CC,W,H) WBrh_(CANVASIMG, 2), (CC), WBwh(W, H)
#define CCANVASIMGv(CC,W,H) WBrh_c(CANVASIMG, 2), (CC), WBwh(W, H)
#define CANVASIMGB(CP,A) WBrhnf_(CANVASIMGB, 2), WBfield(CP), WBfield(A)
#define FCIMAGEP(CP,A,AS) WBrhnf_(FCIMAGEP, 3), WBfield(CP), WBfield(A), \
	WBfield(AS)
#define TLFCIMAGEP(CP,A,AS,X,Y) WBrhnf_t(FCIMAGEP, 4), WBfield(CP), \
	WBfield(A), WBfield(AS), WBxyl(X, Y, 1)
#define TLFCIMAGEPn(CP,AS,X,Y) WBrhnf_t(FCIMAGEP, 4), WBfield(CP), \
	(void *)(-1), WBfield(AS), WBxyl(X, Y, 1)
#define CANVAS(W,H,C,HX) WBr2h_(CANVAS, 2 + 2), WBwh(W, H), (void *)(C), \
	EVENT(EXT, HX)
#define TLNOSPIN(V,X,Y) WBhf_t(NOSPIN, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLNOSPINr(V,X,Y) WBrhf_t(NOSPIN, 2), WBfield(V), WBxyl(X, Y, 1)
#define NOSPINv(V) WBh_(NOSPIN, 1), &(V)
#define TLSPIN(V,V0,V1,X,Y) WBrhf_t(SPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLXSPIN(V,V0,V1,X,Y) WBrhf_tx(SPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLXSPINv(V,V0,V1,X,Y) WBrh_tx(SPIN, 4), &(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define T1SPIN(V,V0,V1) WBrhf_t1(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define TSPIN(NM,V,V0,V1) T1SPIN(V,V0,V1), TLABEL(NM)
#define TSPINv(NM,V,V0,V1) WBrh_t1(SPIN, 3), &(V), \
	(void *)(V0), (void *)(V1), TLABEL(NM)
#define TSPINa(NM,A) WBrhf_t1(SPINa, 1), WBfield(A), TLABEL(NM)
#define SPIN(V,V0,V1) WBrhf_(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define SPINv(V,V0,V1) WBrh_(SPIN, 3), &(V), \
	(void *)(V0), (void *)(V1)
#define SPINc(V,V0,V1) WBrhf_(SPINc, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define XSPIN(V,V0,V1) WBrhf_x(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define FSPIN(V,V0,V1) WBrhf_(FSPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define FSPINv(V,V0,V1) WBrh_(FSPIN, 3), &(V), \
	(void *)(V0), (void *)(V1)
#define TFSPIN(NM,V,V0,V1) WBrhf_t1(FSPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1), TLABEL(NM)
#define TLFSPIN(V,V0,V1,X,Y) WBrhf_t(FSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define SPINa(A) WBrhf_(SPINa, 1), WBfield(A)
#define XSPINa(A) WBrhf_x(SPINa, 1), WBfield(A)
#define uSPIN(V,V0,V1) WBrhf_(uSPIN, 3), WBfield(V), (void *)(V0), (void *)(V1)
#define uSPINv(V,V0,V1) WBrh_(uSPIN, 3), &(V), (void *)(V0), (void *)(V1)
#define uFSPINv(V,V0,V1) WBrh_(uFSPIN, 3), &(V), (void *)(V0), (void *)(V1)
#define uSPINa(A) WBrhf_(uSPINa, 1), WBfield(A)
#define uSCALE(V,V0,V1) WBrhf_(uSCALE, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
/* !!! This block holds 1 nested EVENT block */
#define TLSPINPACKv(A,N,HC,W,X,Y) WBr2h_t(TLSPINPACK, 3 + 2), (A), (void *)(N), \
	EVENT(CHANGE, HC), WBxyl(X, Y, W)
#define T1SPINSLIDE(V,V0,V1) WBrhf_t1(SPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBwh(255, 20)
#define TSPINSLIDE(NM,V,V0,V1) T1SPINSLIDE(V,V0,V1), TLABEL(NM)
#define TLSPINSLIDE(V,V0,V1,X,Y) WBrhf_t(SPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLSPINSLIDEvs(V,V0,V1,X,Y) WBrh_t(SPINSLIDE, 5), &(V), \
	(void *)(V0), (void *)(V1), WBwh(150, 0), WBxyl(X, Y, 1)
#define TLSPINSLIDExl(V,V0,V1,X,Y,L) WBrhf_tx(SPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, L)
#define TLSPINSLIDEx(V,V0,V1,X,Y) TLSPINSLIDExl(V, V0, V1, X, Y, 1)
#define SPINSLIDEa(A) WBrhf_(SPINSLIDEa, 1), WBfield(A)
#define XSPINSLIDEa(A) WBrhf_x(SPINSLIDEa, 1), WBfield(A)
#define CHECK(NM,V) WBrhf_(CHECK, 2), WBfield(V), (NM)
#define CHECKv(NM,V) WBrh_(CHECK, 2), &(V), (NM)
#define CHECKb(NM,V,I) WBrhf_(CHECKb, 3), WBfield(V), (NM), (I)
#define XCHECK(NM,V) WBrhf_x(CHECK, 2), WBfield(V), (NM)
#define TLCHECKl(NM,V,X,Y,L) WBrhf_t(CHECK, 3), WBfield(V), (NM), WBxyl(X, Y, L)
#define TLCHECK(NM,V,X,Y) TLCHECKl(NM, V, X, Y, 1)
#define TLCHECKvl(NM,V,X,Y,L) WBrh_t(CHECK, 3), &(V), (NM), WBxyl(X, Y, L)
#define TLCHECKv(NM,V,X,Y) TLCHECKvl(NM, V, X, Y, 1)
#define uCHECKv(NM,V) WBrh_(uCHECK, 2), &(V), (NM)
/* !!! No more than 255 choices */
#define RPACK(SS,N,H,V) WBrhf_x(RPACK, 3), WBfield(V), (SS), WBnh(N, H)
#define RPACKv(SS,N,H,V) WBrh_x(RPACK, 3), &(V), (SS), WBnh(N, H)
#define FRPACK(NM,SS,N,H,V) FRAME(NM), RPACK(SS,N,H,V)
#define FRPACKv(NM,SS,N,H,V) FRAME(NM), RPACKv(SS,N,H,V)
#define RPACKD(SP,H,V) WBrhf_x(RPACKD, 3), WBfield(V), WBfield(SP), (H)
#define RPACKDv(SP,H,V) WBrh_x(RPACKD, 3), &(V), WBfield(SP), (H)
/* !!! These blocks each hold 1 nested EVENT block */
#define RPACKe(SS,N,H,V,HS) WBr2hf_x(RPACK, 3 + 2), WBfield(V), (SS), \
	WBnh(N, H), EVENT(SELECT, HS)
#define FRPACKe(NM,SS,N,H,V,HS) FRAME(NM), RPACKe(SS,N,H,V,HS)
#define RPACKDve(SP,H,V,HS) WBr2h_x(RPACKD, 3 + 2), &(V), WBfield(SP), \
	(H), EVENT(SELECT, HS)
#define OPT(SS,N,V) WBrhf_(OPT, 3), WBfield(V), (SS), (void *)(N)
#define TOPTv(NM,SS,N,V) WBrh_t1(OPT, 3), &(V), (SS), (void *)(N), TLABEL(NM)
#define TLOPT(SS,N,V,X,Y) WBrhf_t(OPT, 4), WBfield(V), (SS), (void *)(N), \
	WBxyl(X, Y, 1)
#define OPTD(SP,V) WBrhf_(OPTD, 2), WBfield(V), WBfield(SP)
#define TOPTDv(NM,SP,V) WBrh_t1(OPTD, 2), &(V), WBfield(SP), TLABEL(NM)
/* !!! These blocks each hold 1 nested EVENT block */
#define XOPTe(SS,N,V,HS) WBr2hf_x(OPT, 3 + 2), WBfield(V), (SS), (void *)(N), \
	EVENT(SELECT, HS)
#define TLOPTle(SS,N,V,HS,X,Y,L) WBr2hf_t(OPT, 4 + 2), WBfield(V), (SS), \
	(void *)(N), EVENT(SELECT, HS), WBxyl(X, Y, L)
#define TLOPTvle(SS,N,V,HS,X,Y,L) WBr2h_t(OPT, 4 + 2), &(V), (SS), \
	(void *)(N), EVENT(SELECT, HS), WBxyl(X, Y, L)
#define TLOPTve(SS,N,V,HS,X,Y) TLOPTvle(SS, N, V, HS, X, Y, 1)
#define OPTDe(SP,V,HS) WBr2hf_(OPTD, 2 + 2), WBfield(V), WBfield(SP), \
	EVENT(SELECT, HS)
#define COMBO(SS,N,V) WBrhf_(COMBO, 3), WBfield(V), (SS), (void *)(N)
#define GRADBAR(M,V,L,MX,A,CC,HS) WBr2hf_(GRADBAR, 6 + 2), WBfield(V), \
	WBfield(M), WBfield(L), WBfield(A), WBfield(CC), (void *)(MX), \
	EVENT(SELECT, HS)
#define PCTCOMBOv(V,A,HC) WBr2h_(PCTCOMBO, 2 + 2), &(V), (A), EVENT(CHANGE, HC)
#define LISTCCHr(V,L,H,HS) WBr2hf_(LISTCCr, 3 + 2), WBfield(V), WBfield(L), \
	(void *)(H), EVENT(SELECT, HS)
#define LISTCCr(V,L,HS) LISTCCHr(V, L, 0, HS)
#define LISTC(V,L,HS) WBr2hf_(LISTC, 3 + 2), WBfield(V), WBfield(L), \
	NULL, EVENT(SELECT, HS)
#define LISTCd(V,L,HS) WBr2hf_(LISTCd, 3 + 2), WBfield(V), WBfield(L), \
	NULL, EVENT(SELECT, HS)
#define LISTCu(V,L,HS) WBr2hf_(LISTCu, 3 + 2), WBfield(V), WBfield(L), \
	NULL, EVENT(SELECT, HS)
#define LISTCS(V,L,SM,HS) WBr2hf_(LISTCS, 3 + 2), WBfield(V), WBfield(L), \
	WBfield(SM), EVENT(SELECT, HS)
#define LISTCX(V,L,SM,M,HS,HX) WBr3hf_(LISTCX, 4 + 2 * 2), WBfield(V), \
	WBfield(L), WBfield(SM), WBfield(M), EVENT(SELECT, HS), EVENT(EXT, HX)
#define XENTRY(V) WBrhf_x(ENTRY, 1), WBfield(V)
#define MLENTRY(V) WBrhf_(MLENTRY, 1), WBfield(V)
#define TLENTRY(V,MX,X,Y,L) WBrhf_t(ENTRY, 3), WBfield(V), (void *)(MX), \
	WBxyl(X, Y, L)
#define XPENTRY(V,MX) WBrhf_x(PENTRY, 2), WBfield(V), (void *)(MX)
#define TPENTRYv(NM,V,MX) WBrh_t1(PENTRY, 2), (V), (void *)(MX), TLABEL(NM)
#define PATH(NM,T,M,V) FRAME(NM), WBrhf_(PATH, 3), WBfield(V), (T), (void *)(M)
#define PATHv(NM,T,M,V) FRAME(NM), WBrh_(PATH, 3), (V), (T), (void *)(M)
#define PATHs(NM,T,M,V) FRAME(NM), WBrh_(PATHs, 3), (V), (T), (void *)(M)
#define TEXT(V) WBrhf_x(TEXT, 1), WBfield(V)
#define COMBOENTRY(V,SP,H) WBr2hf_x(COMBOENTRY, 2 + 2), WBfield(V), \
	WBfield(SP), EVENT(OK, H)
#define KEYBUTTON(V) WBrhf_(KEYBUTTON, 1), WBfield(V)
#define TABLETBTN(NM) WBrh_(TABLETBTN, 1), (NM)
#define FONTSEL(A) WBrhf_x(FONTSEL, 1), WBfield(A)
#define HEXENTRY(V,HC,X,Y) WBr2hf_t(HEXENTRY, 2 + 2), WBfield(V), \
	EVENT(CHANGE, HC), WBxyl(X, Y, 1)
#define EYEDROPPER(V,HC,X,Y) WBr2hf_t(EYEDROPPER, 2 + 2), WBfield(V), \
	EVENT(CHANGE, HC), WBxyl(X, Y, 1)
#define COLOR(V) WBrhf_(COLOR, 1), WBfield(V)
#define TCOLOR(A) WBrhf_(TCOLOR, 1), WBfield(A)
/* !!! These blocks each hold 2 nested EVENT blocks */
/* !!! SELECT must be last, for it gets triggered */
#define COLORLIST(SP,V,CC,HS,HX) WBr3hf_(COLORLIST, 3 + 2 * 2), WBfield(V), \
	WBfield(SP), WBfield(CC), EVENT(EXT, HX), EVENT(SELECT, HS)
#define COLORLISTN(N,V,CC,HS,HX) WBr3hf_(COLORLISTN, 3 + 2 * 2), WBfield(V), \
	WBfield(N), WBfield(CC), EVENT(EXT, HX), EVENT(SELECT, HS)
#define OKBOX(NOK,HOK,NC,HC) EQBOX, CANCELBTN(NC, HC), OKBTN(NOK, HOK)
#define OKBOXP(NOK,HOK,NC,HC) EQBOXP, CANCELBTN(NC, HC), OKBTN(NOK, HOK)
#define OKBOXB(NOK,HOK,NC,HC) EQBOXB, CANCELBTN(NC, HC), OKBTN(NOK, HOK)
#define OKBOX3(NOK,HOK,NC,HC,NB,HB) EQBOX, CANCELBTN(NC, HC), BUTTON(NB, HB), \
	OKBTN(NOK, HOK)
#define OKBOX3B(NOK,HOK,NC,HC,NB,HB) EQBOXB, CANCELBTN(NC, HC), BUTTON(NB, HB), \
	OKBTN(NOK, HOK)
// !!! These *BTN,*TOGGLE,*BUTTON blocks each hold 1 nested EVENT block */
#define OKBTN(NM,H) WBr2h_x(OKBTN, 1 + 2), (NM), EVENT(OK, H)
#define uOKBTN(H) WBr2h_(uOKBTN, 0 + 2), EVENT(OK, H)
#define CANCELBTN(NM,H) WBr2h_x(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define CANCELBTNp(NP,H) WBr2hnf_x(CANCELBTN, 1 + 2), WBfield(NP), \
	EVENT(CANCEL, H)
#define UCANCELBTN(NM,H) WBr2h_(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define ECANCELBTN(NM,H) WBr2h_e(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define UDONEBTN(NM,H) WBr2h_(DONEBTN, 1 + 2), (NM), EVENT(OK, H)
#define TOGGLE(NM,V,H) WBr2hf_x(TOGGLE, 2 + 2), WBfield(V), (NM), \
	EVENT(CHANGE, H)
#define UTOGGLEv(NM,V,H) WBr2h_(TOGGLE, 2 + 2), &(V), (NM), EVENT(CHANGE, H)
#define BUTTON(NM,H) WBr2h_x(BUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define BUTTONs(NM,H) WBr2hs_x(BUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define BUTTONp(NP,H) WBr2hnf_x(BUTTON, 1 + 2), WBfield(NP), EVENT(CLICK, H)
#define UBUTTON(NM,H) WBr2h_(BUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define EBUTTON(NM,H) WBr2h_e(BUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define TLBUTTON(NM,H,X,Y) WBr2h_t(BUTTON, 2 + 2), (NM), EVENT(CLICK, H), \
	WBxyl(X, Y, 1)
#define TLBUTTONs(NM,H,X,Y) WBr2hs_t(BUTTON, 2 + 2), (NM), EVENT(CLICK, H), \
	WBxyl(X, Y, 1)
#define uBUTTONs(NM,H) WBr2hs_(uBUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define TOOLBAR(HC) WBr2h_(TOOLBAR, 0 + 2), EVENT(CHANGE, HC)
#define TOOLBARx(HC,HR) WBr3h_(TOOLBAR, 0 + 2 * 2), EVENT(CHANGE, HC), \
	EVENT(CLICK, HR)
#define SMARTTBAR(HC) WBr2h_(SMARTTBAR, 0 + 2), EVENT(CHANGE, HC)
#define SMARTTBARx(HC,HR) WBr3h_(SMARTTBAR, 0 + 2 * 2), EVENT(CHANGE, HC), \
	EVENT(CLICK, HR)
#define SMARTTBMORE(NM) WBh(SMARTTBMORE, 1), (NM)
#define TBBUTTON(NM,IC,ID) WBrh(TBBUTTON, 4), NULL, (void *)(ID), (NM), (IC)
#define TBBUTTONx(NM,IC,ID,IR) WBrh(TBBUTTON, 5), NULL, (void *)(ID), (NM), \
	(IC), (void *)(IR)
#define TBTOGGLE(NM,IC,ID,V) WBrhf(TBTOGGLE, 4), WBfield(V), (void *)(ID), \
	(NM), (IC)
#define TBTOGGLEv(NM,IC,ID,V) WBrh(TBTOGGLE, 4), &(V), (void *)(ID), (NM), (IC)
#define TBTOGGLExv(NM,IC,ID,IR,V) WBrh(TBTOGGLE, 5), &(V), (void *)(ID), (NM), \
	(IC), (void *)(IR)
#define TBBOXTOGxv(NM,IC,ID,IR,V) WBrh(TBBOXTOG, 5), &(V), (void *)(ID), (NM), \
	(IC), (void *)(IR)
#define TBRBUTTONv(NM,IC,ID,V) WBrh(TBRBUTTON, 4), &(V), (void *)(ID), (NM), (IC)
#define TBRBUTTONxv(NM,IC,ID,IR,V) WBrh(TBRBUTTON, 5), &(V), (void *)(ID), \
	(NM), (IC), (void *)(IR)
#define TBSPACE WBh(TBSPACE, 0)
#define TWOBOX WBh_(TWOBOX, 0)
#define MENUBAR(HC) WBr2h_(MENUBAR, 0 + 2), EVENT(CHANGE, HC)
#define SMARTMENU(HC) WBr2h_(SMARTMENU, 0 + 2), EVENT(CHANGE, HC)
#define SMDONE WBh(SMDONE, 0)
#define SUBMENU(NM) WBrh_(SUBMENU, 1), (NM)
#define ESUBMENU(NM) WBrh_(ESUBMENU, 1), (NM)
#define SSUBMENU(NM) WBrh_(SSUBMENU, 1), (NM)
#define MENUITEM(NM,ID) WBrh_(MENUITEM, 3), NULL, (void *)(ID), (NM)
#define MENUITEMs(NM,ID) WBrhs_(MENUITEM, 3), NULL, (void *)(ID), (NM)
#define MENUITEMi(NM,ID,IC) WBrh_(MENUITEM, 4), NULL, (void *)(ID), (NM), (IC)
#define MENUITEMis(NM,ID,IC) WBrhs_(MENUITEM, 4), NULL, (void *)(ID), (NM), (IC)
#define MENUCHECKv(NM,ID,V) WBrh_(MENUCHECK, 3), &(V), (void *)(ID), (NM)
#define MENUCHECKvs(NM,ID,V) WBrhs_(MENUCHECK, 3), &(V), (void *)(ID), (NM)
#define MENURITEMv(NM,ID,V) WBrh_(MENURITEM, 3), &(V), (void *)(ID), (NM)
#define MENURITEMvs(NM,ID,V) WBrhs_(MENURITEM, 3), &(V), (void *)(ID), (NM)
#define MENUTEAR WBh_(MENUTEAR, 0)
#define MENUSEP WBh_(MENUSEP, 0)
#define MENUSEPr WBrh_(MENUSEP, 0)
#define uMENUBAR(HC) WBr2h_(uMENUBAR, 0 + 2), EVENT(CHANGE, HC)
#define uMENUITEM(NM,ID) WBrh_(uMENUITEM, 3), NULL, (void *)(ID), (NM)
#define uMENUITEMs(NM,ID) WBrhs_(uMENUITEM, 3), NULL, (void *)(ID), (NM)
#define MOUNT(V,FN,H) WBr2hf_(MOUNT, 2 + 2), WBfield(V), (FN), EVENT(CHANGE, H)
#define PMOUNT(V,FN,H,K,NK) WBr2hf_x(MOUNT, 4 + 2), WBfield(V), (FN), (K), \
	(void *)(NK), EVENT(CHANGE, H)
#define REMOUNTv(V) WBrh_x(REMOUNT, 1), &(V)
//#define EXEC(FN) WBh(EXEC, 1), (FN)
#define GOTO(A) WBh(GOTO, 1), (A)
#define CALL(A) WBh(CALL, 1), (A)
#define CALLp(V) WBhnf(CALL, 1), WBfield(V)
#define RET WBh(RET, 0)
#define IF(X) WBhf(IF, 1), WBfield(X)
#define IFx(X,N) WBhf(IF, 2), WBfield(X), (void *)(N)
#define IFv(X) WBh(IF, 1), &(X)
#define IFvx(X,N) WBh(IF, 2), &(X), (void *)(N)
#define UNLESS(X) WBhf(UNLESS, 1), WBfield(X)
#define UNLESSx(X,N) WBhf(UNLESS, 2), WBfield(X), (void *)(N)
#define UNLESSv(X) WBh(UNLESS, 1), &(X)
#define UNLESSbt(V) WBh(UNLESSbt, 1), (V)
#define ENDIF(N) WBh(ENDIF, 1), (void *)(N)
#define REF(V) WBhf(REF, 1), WBfield(V)
#define REFv(V) WBh(REF, 1), &(V)
#define CLEANUP(V) WBrhf(CLEANUP, 1), WBfield(V)
#define TALLOC(V,L) WBhf(TALLOC, 2), WBfield(V), WBfield(L)
#define TCOPY(V,L) WBhf(TCOPY, 2), WBfield(V), WBfield(L)
#define ACTMAP(N) WBh(ACTMAP, 1), (void *)(N)
#define VISMASK(N) WBh(ACTMAP, 2), (void *)(N), NULL
#define KEYMAP(V, NM) WBrhf(KEYMAP, 2), WBfield(V), (NM)
#define SHORTCUTs(NM) WBh(SHORTCUT, 1), (NM)
#define SHORTCUT(K,M) WBh(SHORTCUT, 2), (void *)(GDK_##K), (void *)(_##M##mask)
#define GROUPR WBrh(uOP, 0)
#define GROUP0 WBrhs(uOP, 0)
#define GROUPN WBrhs(uOP, 1), NULL
#define GROUP(NM) WBrhs(uOP, 1), (NM)
#define IDENT(NM) WBh(IDENT, 1), (NM)
#define BORDER(T,V) WBh(BOR_##T, 1), (void *)(V)
#define DEFBORDER(T) WBh(BOR_##T, 0)
#define MKSHRINK WBh(MKSHRINK, 0)
#define NORESIZE WBh(NORESIZE, 0)
#define WANTMAX WBh(WANTMAX, 0)
#define WANTMAXW WBh(WANTMAX, 1), (void *)(2)
#define WXYWH(NM,W,H) WBh(WXYWH, 2), (NM), WBwh(W, H)
#define DEFW(V) WXYWH(NULL, V, 0)
#define DEFH(V) WXYWH(NULL, 0, V)
#define DEFSIZE(W,H) WXYWH(NULL, W, H)
#define WPMOUSE WBh(WPMOUSE, 0)
#define WPWHEREVER WBh(WPWHEREVER, 0)
#define HIDDEN WBh(HIDDEN, 0)
#define INSENS WBh(INSENS, 0)
#define FOCUS WBh(FOCUS, 0)
#define WIDTH(N) WBh(WIDTH, 1), (void *)(N)
#define MINWIDTH(N) WBh(WIDTH, 1), (void *)(-(N))
#define KEEPWIDTH WBh(KEEPWIDTH, 0)
#define HEIGHT(N) WBh(HEIGHT, 1), (void *)(N)
#define ONTOP(V) WBhf(ONTOP, 1), WBfield(V)
#define ONTOP0 WBh(ONTOP, 0)
#define RAISED WBh(RAISED, 0)
#define WLIST WBh(WLIST, 0)
#define COLUMNDATA(V,S) WBhnf(COLUMNDATA, 2), WBfield(V), (void *)(S)
#define IDXCOLUMN(N0,S,W,J) WBrh(IDXCOLUMN, 3), (void *)(N0), (void *)(S), \
	(void *)((W) + ((J) << 16))
#define TXTCOLUMNv(A,S,W,J) WBrh(TXTCOLUMN, 3), &(A), (void *)(S), \
	(void *)((W) + ((J) << 16))
#define XTXTCOLUMNv(A,S,W,J) WBrh(XTXTCOLUMN, 3), &(A), (void *)(S), \
	(void *)((W) + ((J) << 16))
#define NTXTCOLUMNv(NM,A,S,W,J) WBrh(TXTCOLUMN, 4), &(A), (void *)(S), \
	(void *)((W) + ((J) << 16)), (NM)
#define NTXTCOLUMND(NM,ST,F,W,J) WBrh(TXTCOLUMN, 4), (void *)offsetof(ST, F), \
	NULL, (void *)((W) + ((J) << 16)), (NM)
#define PTXTCOLUMNp(V,S,W,J) WBrhnf(TXTCOLUMN, 3), WBfield(V), (void *)(S), \
	(void *)((W) + ((J) << 16) + (col_PTR << COL_LSHIFT))
#define RTXTCOLUMND(ST,F,W,J) WBrh(TXTCOLUMN, 3), (void *)offsetof(ST, F), \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT))
#define RTXTCOLUMNDi(W,J) WBrh(TXTCOLUMN, 3), (void *)0, \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT))
#define NRTXTCOLUMND(NM,ST,F,W,J) WBrh(TXTCOLUMN, 4), (void *)offsetof(ST, F), \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT)), (NM)
#define NRTXTCOLUMNDax(NM,F,W,J,I) WBrh(TXTCOLUMN, 5), (void *)(sizeof(int) * F), \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT)), (NM), (I)
#define NRTXTCOLUMNDaxx(NM,F,W,J,I,S) WBrh(TXTCOLUMN, 6), (void *)(sizeof(int) * F), \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT)), (NM), (I), (S)
#define NRFILECOLUMNDax(NM,F,W,J,I) WBrh(FILECOLUMN, 5), (void *)(sizeof(int) * F), \
	NULL, (void *)((W) + ((J) << 16) + (col_REL << COL_LSHIFT)), (NM), (I)
#define CHKCOLUMNv(A,S,W,J,HC) WBr2h(CHKCOLUMN, 3 + 2), &(A), (void *)(S), \
	(void *)((W) + ((J) << 16)), EVENT(CHANGE, HC)
#define	XBMCURSOR(T,X,Y) WBrh(XBMCURSOR, 3), (xbm_##T##_bits), \
	(xbm_##T##_mask_bits), WBxyl(X, Y, 20 + 1)
#define	SYSCURSOR(T) WBrh(SYSCURSOR, 1), (void *)(GDK_##T)
#define EVENT(T,H) WBrh(EVT_##T, 1), (H)
#define TRIGGER WBrh(TRIGGER, 0)
#define MTRIGGER(H) WBr2h(TRIGGER, 0 + 2), EVENT(CHANGE, H)
#define WANTKEYS(H) WBr2h(WANTKEYS, 0 + 2), EVENT(KEY, H)
#define CLIPFORM(A,N) WBrh(CLIPFORM, 2), &(A), (void *)(N)
#define DRAGDROP(F,HF,HT) WBr3hf(DRAGDROP, 2 + 2 * 2), WBfield(F), NULL, \
	EVENT(DRAGFROM, HF), EVENT(DROP, HT)
#define DRAGDROPm(F,HF,HT) WBr3hf(DRAGDROP, 2 + 2 * 2), WBfield(F), (void *)1, \
	EVENT(DRAGFROM, HF), EVENT(DROP, HT)
#define CLIPBOARD(F,T,HC,HP) WBr3hf(CLIPBOARD, 2 + 2 * 2), WBfield(F), \
	(void *)(T), EVENT(COPY, HC), EVENT(PASTE, HP)
#define ALTNAME(NM) WBrh(uALTNAME, 1), (NM)
/* Make option strings referrable as widget names */
#define FLATTEN ALTNAME(":")
#define OPNAME(NM) WBh(uOPNAME, 1), (NM)
#define OPNAME0 WBh(uOPNAME, 0)
/* Set an impossible name, to hide widget from script */
#define UNNAME OPNAME("=")
#define SCRIPTED WBhs(uOPNAME, 0)
#define ENDSCRIPT WBrhs(uOPNAME, 0)

//	Extra data of FPICK
#define FPICK_VALUE	0
#define FPICK_RAW	1
//	Mode flags for FPICK
#define FPICK_ENTRY	1
#define FPICK_LOAD	2
#define FPICK_DIRS_ONLY	4

//	Extra data of PATH and PENTRY
#define PATH_VALUE	0
#define PATH_RAW	1

//	Extra data of LISTCC
#define LISTCC_RESET_ROW 0

//	Extra data of LISTC
#define LISTC_RESET_ROW	0
#define LISTC_ORDER	1
#define LISTC_SORT	2

//	Extra data of LABEL and MENUITEM
#define LABEL_VALUE	0

//	Extra data of NBOOK
#define NBOOK_TABS	0

//	Extra data of ENTRY and COMBOENTRY
#define ENTRY_VALUE	0

//	Extra data of WDATA itself
// !!! Must not clash with toplevels' data
#define WDATA_ACTMAP	(-1)	/* Change state of slots which have ACTMAP */
#define WDATA_TABLET	(-2)	/* Query tablet device */

//	Extra data of WINDOW
#define WINDOW_TITLE	0
#define WINDOW_ESC_BTN	1
#define WINDOW_FOCUS	2
#define WINDOW_RAISE	3
#define WINDOW_DISAPPEAR 4
#define WINDOW_DPI	5
#define WINDOW_TEXTENG	6

//	Extra data of COLOR
#define COLOR_RGBA	0
#define COLOR_ALL	1

//	Extra data of SPIN and SPINSLIDE
#define SPIN_ALL	0

//	Extra data of COLORLIST
#define COLORLIST_RESET_ROW 0

//	Extra data of PROGRESS
#define PROGRESS_PERCENT 0

//	Extra data of CSCROLL
#define CSCROLL_XYSIZE	0	/* 4 ints: xywh; read-only */
#define CSCROLL_LIMITS	1	/* 2 ints: wh; read-only */
#define CSCROLL_XYRANGE	2	/* 4 ints: xywh */

//	Extra data of CANVASIMG and CANVAS
#define CANVAS_SIZE	0
#define CANVAS_VPORT	1
#define CANVAS_REPAINT	2
#define CANVAS_PAINT	3
#define CANVAS_FIND_MOUSE  4	/* mouse_ext: as motion event */
#define CANVAS_BMOVE_MOUSE 5

//	Extra data of FCIMAGE
#define FCIMAGE_XY	0

//	Extra data of KEYMAP
#define KEYMAP_KEY	0
#define KEYMAP_MAP	1

//	Extra state of EV_MOUSE
#define MOUSE_BOUND	0

//	Extra data of EV_DRAGFROM
#define DRAG_DATA	0	/* array of 2 pointers: start/end */
#define DRAG_ICON_RGB	1

//	Extra data of EV_COPY
#define COPY_DATA	0

//	Extra state of CLIPBOARD
#define CLIP_OFFER	0
#define CLIP_PROCESS	1

//	Extra state of all regular widgets
#define SLOT_SENSITIVE	0
#define SLOT_FOCUSED	1
#define SLOT_SCRIPTABLE	2
#define SLOT_UNREAL	3
#define SLOT_RADIO	4

//	Extra data of FONTSEL
#define FONTSEL_DPI	0
