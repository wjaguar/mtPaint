/*	vcode.h
	Copyright (C) 2013-2014 Dmitry Groshev

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

	op_END_LAST,
	op_WDONE = op_END_LAST,

	op_MAINWINDOW,
	op_WINDOW,
	op_WINDOWm,
	op_DIALOGm,
	op_FPICKpm,
	op_TOPVBOX,
	op_TOPVBOXV,
	op_DOCK,
	op_PAGE,
	op_PAGEi,
	op_TABLE,
	op_XTABLE,
	op_ETABLE,
	op_FTABLE,
	op_FSXTABLEp,
	//
	op_VBOX,
	op_XVBOX,
	op_EVBOX,
	op_HBOX,
	op_XHBOX,
	op_TLHBOX,
	op_FVBOX,
	op_FXVBOX,
	op_FHBOX,
	//
	op_EQBOX,
	op_SCROLL,
	op_XSCROLL,
	op_SNBOOK,
	op_NBOOK,
	op_PLAINBOOK,
	op_BOOKBTN,
	op_HSEP,
	op_MLABEL,
	op_WLABEL,
	op_XLABEL,
	op_TLLABEL,
	op_TLTEXT,
	op_TLNOSPIN,
	//
	op_SPIN,
	op_SPINc,
	op_XSPIN,
	op_TSPIN,
	op_TLSPIN,
	op_TLXSPIN,
	//
	op_FSPIN,
	op_TFSPIN,
	op_TLFSPIN,
	//
	op_SPINa,
	op_XSPINa,
	op_TSPINa,
	//
	op_TLSPINPACK,
	op_TSPINSLIDE,
	op_HTSPINSLIDE,
	op_TLSPINSLIDE,
	op_TLSPINSLIDEs,
	op_TLSPINSLIDEx,
	op_SPINSLIDEa,
	op_XSPINSLIDEa,
	op_CHECK,
	op_XCHECK,
	op_TLCHECK,
	op_TLCHECKs,
	op_CHECKb,
	op_RPACK,
	op_FRPACK,
	op_RPACKD,
	op_OPT,
	op_XOPT,
	op_TOPT,
	op_TLOPT,
	op_OPTD,
	op_COMBO,
	op_XENTRY,
	op_MLENTRY,
	op_TLENTRY,
	op_XPENTRY,
	op_TPENTRY,
	op_PATH,
	op_PATHs,
	op_TEXT,
	op_COLOR,
	op_TCOLOR,
	op_COLORLIST,
	op_COLORLISTN,
	op_COLORPAD,
	op_GRADBAR,
	op_LISTCCr,
	op_LISTCCHr,
	op_LISTC,
	op_LISTCd,
	op_LISTCu,
	op_LISTCS,
	op_OKBOX,
	op_OKBOXp,
	op_EOKBOX,
	op_UOKBOX,
	op_UOKBOXp,
	op_OKBTN,
	op_CANCELBTN,
	op_UCANCELBTN,
	op_ECANCELBTN,
	op_OKADD,
	op_OKTOGGLE,
	op_UTOGGLE,
	op_BUTTON,
	op_UBUTTON,
	op_EBUTTON,
	op_TLBUTTON,
	op_MOUNT,
	op_PMOUNT,
	op_REMOUNT,

	op_GROUP,
	op_IDENT,

	op_WLIST,
	op_IDXCOLUMN,
	op_TXTCOLUMN,
	op_XTXTCOLUMN,
	op_RTXTCOLUMN,
	op_CHKCOLUMNi0,

	op_EVT_0,
	op_EVT_OK = op_EVT_0,
	op_EVT_CANCEL,
	op_EVT_CLICK,
	op_EVT_SELECT,
	op_EVT_CHANGE,
	op_EVT_DESTROY, // before deallocation
	op_EVT_KEY, // with key data
	op_EVT_EXT, // generic event with extra data

	op_EVT_LAST,
	op_TRIGGER = op_EVT_LAST,

	op_EXEC,
	op_GOTO,
	op_CALLp,
	op_RET,
	op_IF,
	op_UNLESS,
	op_UNLESSbt,
	op_ENDIF,
	op_REF,
	op_CLEANUP,
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
	op_ONTOP,
	op_RAISED,

	op_BOR_0,
	op_BOR_TABLE = op_BOR_0,
	op_BOR_NBOOK,
	op_BOR_XSCROLL,
	op_BOR_SPIN,
	op_BOR_SPINSLIDE,
	op_BOR_LABEL,
	op_BOR_TLABEL,
	op_BOR_OPT,
	op_BOR_XOPT,
	op_BOR_FRBOX,
	op_BOR_OKBOX,
	op_BOR_BUTTON,

	op_BOR_LAST,
	op_LAST = op_BOR_LAST
};

//	Function to run with EXEC
typedef void **(*ext_fn)(void **r, GtkWidget ***wpp, void **wdata);
//	Function to run with MOUNT
typedef void **(*mnt_fn)(void **wdata);

//	Structure which COLORLIST provides to EXC event
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

typedef void (*evt_fn)(void *ddata, void **wdata, int what, void **where);
typedef int (*evtkey_fn)(void *ddata, void **wdata, int what, void **where,
	key_ext *keydata);
typedef void (*evtx_fn)(void *ddata, void **wdata, int what, void **where,
	void *xdata);

//	Build a dialog window out of V-code decription
//void **run_create(const void **ifcode, const void *ddata, int ddsize);
void **run_create(void **ifcode, void *ddata, int ddsize);
//	Query dialog contents using its widget-map
void run_query(void **wdata);
//	Destroy a dialog by its widget-map
void run_destroy(void **wdata);

//	Extract data structure out of widget-map
#define GET_DDATA(V) ((V)[0])
//	Extract toplevel window slot out of widget-map
#define GET_WINDOW(V) ((V) + 2)
//	Extract actual toplevel window out of widget-map
#define GET_REAL_WINDOW(V) ((V)[2])
//	Iterate over slots
#define NEXT_SLOT(V) ((V) + 2)
#define PREV_SLOT(V) ((V) - 2)
#define SLOT_N(V,N) ((V) + (N) * 2)

//	From widget to its wdata
void **get_wdata(GtkWidget *widget, char *id);

//	Install event handler
void add_click(void **r, GtkWidget *widget);
void add_del(void **r, GtkWidget *window);

//	Run event handler, defaulting to run_destroy()
void do_evt_1_d(void **slot);

//	From event to its originator
void **origin_slot(void **slot);

//	Set sensitive state on slot
void cmd_sensitive(void **slot, int state);
//	Set visible state on slot
void cmd_showhide(void **slot, int state);
//	Set value on spinslider slot, page on plainbook slot
void cmd_set(void **slot, int v);
//	Set color & opacity on color slot
void cmd_set2(void **slot, int v0, int v1);
//	Set value & limits on spin-like slot
void cmd_set3(void **slot, int *v);
//	Set current & previous color/opacity on color slot
void cmd_set4(void **slot, int *v);
//	Set per-item visibility and selection on list-like slot
void cmd_setlist(void **slot, char *map, int n);
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
//	Scroll in position on colorlist slot
void cmd_scroll(void **slot, int idx);
//	Set cursor for slot window
void cmd_cursor(void **slot, GdkCursor *cursor); // !!! GTK-specific for now

///	Handler for dialog buttons
void dialog_event(void *ddata, void **wdata, int what, void **where);

#define WB_OPBITS     12
#define WB_OPMASK 0x0FFF /* ((1 << WB_OPBITS) - 1) */
#define WB_FFLAG  0x8000
#define WB_NFLAG  0x4000
#define WB_REF1   0x1000
#define WB_REF2   0x2000
#define WB_REF3   0x3000
#define WB_GETREF(X) (((X) >> WB_OPBITS) & 3)
#define WB_LSHIFT     16
#define WB_GETLEN(X) ((X) >> WB_LSHIFT)

#define WBlen(L) ((L) << WB_LSHIFT)
#define WBh(NM,L) (void *)(op_##NM + WBlen(L))
#define WBhf(NM,L) (void *)(op_##NM + WBlen(L) + WB_FFLAG)
#define WBrh(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF1)
#define WBrhf(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF1 + WB_FFLAG)
#define WBr2h(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF2)
#define WBr2hf(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF2 + WB_FFLAG)
#define WBr3h(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF3)
#define WBr3hf(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF3 + WB_FFLAG)
#define WBhnf(NM,L) (void *)(op_##NM + WBlen(L) + WB_NFLAG + WB_FFLAG)
#define WBrhnf(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF1 + WB_NFLAG + WB_FFLAG)
#define WBr2hnf(NM,L) (void *)(op_##NM + WBlen(L) + WB_REF2 + WB_NFLAG + WB_FFLAG)
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
#define MAINWINDOW(NM,ICN,W,H) WBr2h(MAINWINDOW, 3), (NM), (ICN), WBwh(W, H)
#define WINDOW(NM) WBrh(WINDOW, 1), (NM)
#define WINDOWm(NM) WBrh(WINDOWm, 1), (NM)
#define WINDOWpm(NP) WBrhnf(WINDOWm, 1), WBfield(NP)
#define DIALOGpm(NP) WBrhnf(DIALOGm, 1), WBfield(NP)
/* !!! Note: string "V" is in system encoding */
/* !!! This block holds 2 nested EVENT blocks */
#define FPICKpm(NP,F,V,HOK,HC) WBr3hf(FPICKpm, 3 + 2 * 2), WBfield(V), \
	WBfield(NP), WBfield(F), EVENT(OK, HOK), EVENT(CANCEL, HC)
#define TOPVBOX WBrh(TOPVBOX, 0)
#define TOPVBOXV WBrh(TOPVBOXV, 0)
#define DOCK(K) WBrh(DOCK, 1), (K)
#define PAGE(NM) WBh(PAGE, 1), (NM)
#define PAGEi(ICN,S) WBh(PAGEi, 2), (ICN), (void *)(S)
#define PAGEir(ICN,S) WBrh(PAGEi, 2), (ICN), (void *)(S)
#define TABLE(W,H) WBh(TABLE, 1), WBwh(W, H)
#define TABLE2(H) TABLE(2, (H))
#define TABLEs(W,H,S) WBh(TABLE, 2), WBwh(W, H), (void *)(S)
#define TABLEr(W,H) WBrh(TABLE, 1), WBwh(W, H)
#define XTABLE(W,H) WBh(XTABLE, 1), WBwh(W, H)
#define ETABLE(W,H) WBh(ETABLE, 1), WBwh(W, H)
#define FTABLE(NM,W,H) WBh(FTABLE, 2), WBwh(W, H), (NM)
#define FSXTABLEp(V,W,H) WBhf(FSXTABLEp, 2), WBfield(V), WBwh(W, H)
#define VBOX WBh(VBOX, 0)
#define VBOXbp(S,B,P) WBh(VBOX, 1), WBpbs(P, B, S)
#define VBOXPS VBOXbp(5, 0, 5)
#define XVBOX WBh(XVBOX, 0)
#define XVBOXbp(S,B,P) WBh(XVBOX, 1), WBpbs(P, B, S)
#define XVBOXb(S,B) XVBOXbp(S, B, 0)
#define EVBOX WBh(EVBOX, 0)
#define HBOX WBh(HBOX, 0)
#define HBOXbp(S,B,P) WBh(HBOX, 1), WBpbs(P, B, S)
#define HBOXb(S,B) HBOXbp(S, B, 0)
#define HBOXPr WBrh(HBOX, 1), WBpbs(5, 0, 0)
#define XHBOX WBh(XHBOX, 0)
#define XHBOXbp(S,B,P) WBh(XHBOX, 1), WBpbs(P, B, S)
#define XHBOXb(S,B) XHBOXbp(S, B, 0)
#define TLHBOXl(X,Y,L) WBh(TLHBOX, 1), WBxyl(X, Y, L)
#define FVBOX(NM) WBh(FVBOX, 1), (NM)
#define FVBOXs(NM,S) WBh(FVBOX, 2), WBbs(0, S), (NM)
#define FXVBOX(NM) WBh(FXVBOX, 1), (NM)
#define FHBOX(NM) WBh(FHBOX, 1), (NM)
#define EQBOX WBh(EQBOX, 0)
#define EQBOXs(S) WBh(EQBOX, 1), WBbs(0, S)
#define EQBOXb(S,B) WBh(EQBOX, 1), WBbs(B, S)
#define SCROLL(HP,VP) WBh(SCROLL, 1), (void *)((HP) + ((VP) << 8))
#define XSCROLL(HP,VP) WBh(XSCROLL, 1), (void *)((HP) + ((VP) << 8))
#define SNBOOK WBh(SNBOOK, 0)
#define NBOOK WBh(NBOOK, 0)
#define NBOOKr WBrh(NBOOK, 0)
#define PLAINBOOK WBrh(PLAINBOOK, 0)
#define PLAINBOOKn(N) WBrh(PLAINBOOK, 1), (void *)(N)
#define BOOKBTN(NM,V) WBhf(BOOKBTN, 2), WBfield(V), (NM)
#define HSEP WBh(HSEP, 0)
#define HSEPl(V) WBh(HSEP, 1), (void *)(V)
#define HSEPt WBh(HSEP, 1), (void *)(-1)
#define MLABEL(NM) WBh(MLABEL, 1), (NM)
#define MLABELr(NM) WBrh(MLABEL, 1), (NM)
#define MLABELxr(NM,PX,PY,AX) WBrh(MLABEL, 2), (NM), WBppa(PX, PY, AX)
#define MLABELp(V) WBhnf(MLABEL, 1), WBfield(V)
#define WLABELp(V) WBhnf(WLABEL, 1), WBfield(V)
#define XLABELr(NM) WBrh(XLABEL, 1), (NM)
#define TLLABELl(NM,X,Y,L) WBh(TLLABEL, 2), (NM), WBxyl(X, Y, L)
#define TLLABEL(NM,X,Y) TLLABELl(NM, X, Y, 1)
#define TLLABELr(NM,X,Y) WBrh(TLLABEL, 2), (NM), WBxyl(X, Y, 1)
#define TLLABELx(NM,X,Y,PX,PY,AX) WBh(TLLABEL, 3), (NM), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TLLABELxr(NM,X,Y,PX,PY,AX) WBrh(TLLABEL, 3), (NM), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TLLABELp(V,X,Y) WBhnf(TLLABEL, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLLABELpx(V,X,Y,PX,PY,AX) WBhnf(TLLABEL, 3), WBfield(V), \
	WBppa(PX, PY, AX), WBxyl(X, Y, 1)
#define TLTEXT(S,X,Y) WBh(TLTEXT, 2), (S), WBxyl(X, Y, 1)
#define TLTEXTf(C,X,Y) WBhf(TLTEXT, 2), WBfield(C), WBxyl(X, Y, 1)
#define TLTEXTp(V,X,Y) WBhnf(TLTEXT, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLNOSPIN(V,X,Y) WBhf(TLNOSPIN, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLNOSPINr(V,X,Y) WBrhf(TLNOSPIN, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLSPIN(V,V0,V1,X,Y) WBrhf(TLSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLXSPIN(V,V0,V1,X,Y) WBrhf(TLXSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TSPIN(NM,V,V0,V1) WBrhf(TSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TSPINv(NM,V,V0,V1) WBrh(TSPIN, 4), &(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TSPINa(NM,A) WBrhf(TSPINa, 2), WBfield(A), (NM)
#define SPIN(V,V0,V1) WBrhf(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define SPINv(V,V0,V1) WBrh(SPIN, 3), &(V), \
	(void *)(V0), (void *)(V1)
#define SPINc(V,V0,V1) WBrhf(SPINc, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define XSPIN(V,V0,V1) WBrhf(XSPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define FSPIN(V,V0,V1) WBrhf(FSPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define FSPINv(V,V0,V1) WBrh(FSPIN, 3), &(V), \
	(void *)(V0), (void *)(V1)
#define TFSPIN(NM,V,V0,V1) WBrhf(TFSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TLFSPIN(V,V0,V1,X,Y) WBrhf(TLFSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define SPINa(A) WBrhf(SPINa, 1), WBfield(A)
#define XSPINa(A) WBrhf(XSPINa, 1), WBfield(A)
/* !!! This block holds 1 nested EVENT block */
#define TLSPINPACKv(A,N,HC,W,X,Y) WBr2h(TLSPINPACK, 3 + 2), (A), (void *)(N), \
	WBxyl(X, Y, W), EVENT(CHANGE, HC)
#define TSPINSLIDE(NM,V,V0,V1) WBrhf(TSPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), (NM)
#define HTSPINSLIDE(NM,V,V0,V1) WBrhf(HTSPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TLSPINSLIDE(V,V0,V1,X,Y) WBrhf(TLSPINSLIDE, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLSPINSLIDEs(V,V0,V1,X,Y) WBrhf(TLSPINSLIDEs, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TLSPINSLIDExl(V,V0,V1,X,Y,L) WBrhf(TLSPINSLIDEx, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, L)
#define TLSPINSLIDEx(V,V0,V1,X,Y) TLSPINSLIDExl(V, V0, V1, X, Y, 1)
#define SPINSLIDEa(A) WBrhf(SPINSLIDEa, 1), WBfield(A)
#define XSPINSLIDEa(A) WBrhf(XSPINSLIDEa, 1), WBfield(A)
#define CHECK(NM,V) WBrhf(CHECK, 2), WBfield(V), (NM)
#define CHECKv(NM,V) WBrh(CHECK, 2), &(V), (NM)
#define CHECKb(NM,V,V0) WBrh(CHECKb, 3), (V), (void *)(V0), (NM)
#define XCHECK(NM,V) WBrhf(XCHECK, 2), WBfield(V), (NM)
#define TLCHECKl(NM,V,X,Y,L) WBrhf(TLCHECK, 3), WBfield(V), (NM), WBxyl(X, Y, L)
#define TLCHECK(NM,V,X,Y) TLCHECKl(NM, V, X, Y, 1)
#define TLCHECKvl(NM,V,X,Y,L) WBrh(TLCHECK, 3), &(V), (NM), WBxyl(X, Y, L)
#define TLCHECKv(NM,V,X,Y) TLCHECKvl(NM, V, X, Y, 1)
#define TLCHECKsv(NM,V,X,Y) WBrh(TLCHECKs, 3), &(V), (NM), WBxyl(X, Y, 1)
/* !!! No more than 255 choices */
#define RPACK(SS,N,H,V) WBrhf(RPACK, 3), WBfield(V), (SS), WBnh(N, H)
#define RPACKv(SS,N,H,V) WBrh(RPACK, 3), &(V), (SS), WBnh(N, H)
#define FRPACK(NM,SS,N,H,V) WBrhf(FRPACK, 4), WBfield(V), (SS), WBnh(N, H), (NM)
#define FRPACKv(NM,SS,N,H,V) WBrh(FRPACK, 4), &(V), (SS), WBnh(N, H), (NM)
#define RPACKD(SP,H,V) WBrhf(RPACKD, 3), WBfield(V), WBfield(SP), (H)
#define RPACKDv(SP,H,V) WBrh(RPACKD, 3), &(V), WBfield(SP), (H)
/* !!! These blocks each hold 1 nested EVENT block */
#define RPACKe(SS,N,H,V,HS) WBr2hf(RPACK, 3 + 2), WBfield(V), (SS), \
	WBnh(N, H), EVENT(SELECT, HS)
#define FRPACKe(NM,SS,N,H,V,HS) WBr2hf(FRPACK, 4 + 2), WBfield(V), (SS), \
	WBnh(N, H), EVENT(SELECT, HS), (NM)
#define RPACKDve(SP,H,V,HS) WBr2h(RPACKD, 3 + 2), &(V), WBfield(SP), \
	(H), EVENT(SELECT, HS)
#define OPT(SS,N,V) WBrhf(OPT, 3), WBfield(V), (SS), (void *)(N)
#define XOPT(SS,N,V) WBrhf(XOPT, 3), WBfield(V), (SS), (void *)(N)
#define TOPTv(NM,SS,N,V) WBrh(TOPT, 4), &(V), (SS), (void *)(N), (NM)
#define TLOPT(SS,N,V,X,Y) WBrhf(TLOPT, 4), WBfield(V), (SS), (void *)(N), \
	WBxyl(X, Y, 1)
/* !!! These blocks each hold 1 nested EVENT block */
#define XOPTe(SS,N,V,HS) WBr2hf(XOPT, 3 + 2), WBfield(V), (SS), (void *)(N), \
	EVENT(SELECT, HS)
#define TLOPTle(SS,N,V,HS,X,Y,L) WBr2hf(TLOPT, 4 + 2), WBfield(V), (SS), \
	(void *)(N), EVENT(SELECT, HS), WBxyl(X, Y, L)
#define TLOPTvle(SS,N,V,HS,X,Y,L) WBr2h(TLOPT, 4 + 2), &(V), (SS), \
	(void *)(N), EVENT(SELECT, HS), WBxyl(X, Y, L)
#define TLOPTve(SS,N,V,HS,X,Y) TLOPTvle(SS, N, V, HS, X, Y, 1)
#define OPTDe(SP,V,HS) WBr2hf(OPTD, 2 + 2), WBfield(V), WBfield(SP), \
	EVENT(SELECT, HS)
#define COMBO(SS,N,V) WBrhf(COMBO, 3), WBfield(V), (SS), (void *)(N)
#define COLORPAD(CC,V,HS) WBr2hf(COLORPAD, 2 + 2), WBfield(V), WBfield(CC), \
	EVENT(SELECT, HS)
#define GRADBAR(M,V,L,MX,A,CC,HS) WBr2hf(GRADBAR, 6 + 2), WBfield(V), \
	WBfield(M), WBfield(L), WBfield(A), WBfield(CC), (void *)(MX), \
	EVENT(SELECT, HS)
#define LISTCCr(V,L,HS) WBr2hf(LISTCCr, 2 + 2), WBfield(V), WBfield(L), \
	EVENT(SELECT, HS)
#define LISTCCHr(V,L,H,HS) WBr2hf(LISTCCHr, 3 + 2), WBfield(V), WBfield(L), \
	(void *)(H), EVENT(SELECT, HS)
#define LISTC(V,L,FP,S,HS) WBr2hf(LISTC, 5 + 2), WBfield(V), WBfield(L), \
	NULL, WBfield(FP), (void *)(S), EVENT(SELECT, HS)
#define LISTCd(V,L,FP,S,HS) WBr2hf(LISTCd, 5 + 2), WBfield(V), WBfield(L), \
	NULL, WBfield(FP), (void *)(S), EVENT(SELECT, HS)
#define LISTCu(V,L,FP,S,HS) WBr2hf(LISTCu, 5 + 2), WBfield(V), WBfield(L), \
	NULL, WBfield(FP), (void *)(S), EVENT(SELECT, HS)
#define LISTCS(V,L,FP,S,SM,HS) WBr2hf(LISTCS, 5 + 2), WBfield(V), WBfield(L), \
	WBfield(SM), WBfield(FP), (void *)(S), EVENT(SELECT, HS)
#define XENTRY(V) WBrhf(XENTRY, 1), WBfield(V)
#define MLENTRY(V) WBrhf(MLENTRY, 1), WBfield(V)
#define TLENTRY(V,MX,X,Y,L) WBrhf(TLENTRY, 3), WBfield(V), (void *)(MX), \
	WBxyl(X, Y, L)
#define XPENTRY(V,MX) WBrhf(XPENTRY, 2), WBfield(V), (void *)(MX)
#define TPENTRYv(NM,V,MX) WBrh(TPENTRY, 3), (V), (void *)(MX), (NM)
#define PATH(NM,T,M,V) WBrhf(PATH, 4), WBfield(V), (T), (void *)(M), (NM)
#define PATHv(NM,T,M,V) WBrh(PATH, 4), (V), (T), (void *)(M), (NM)
#define PATHs(NM,T,M,V) WBrh(PATHs, 4), (V), (T), (void *)(M), (NM)
#define TEXT(V) WBrhf(TEXT, 1), WBfield(V)
#define COLOR(V) WBrhf(COLOR, 1), WBfield(V)
#define TCOLOR(A) WBrhf(TCOLOR, 1), WBfield(A)
/* !!! These blocks each hold 2 nested EVENT blocks */
/* !!! SELECT must be last, for it gets triggered */
#define COLORLIST(SP,V,CC,HS,HX) WBr3hf(COLORLIST, 3 + 2 * 2), WBfield(V), \
	WBfield(CC), WBfield(SP), EVENT(EXT, HX), EVENT(SELECT, HS)
#define COLORLISTN(N,V,CC,HS,HX) WBr3hf(COLORLISTN, 3 + 2 * 2), WBfield(V), \
	WBfield(CC), WBfield(N), EVENT(EXT, HX), EVENT(SELECT, HS)
#define OKBOX(NOK,HOK,NC,HC) WBr3h(OKBOX, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
#define OKBOXp(NOK,HOK,NC,HC) WBr3h(OKBOXp, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
#define EOKBOX(NOK,HOK,NC,HC) WBr3h(EOKBOX, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
#define OKBOX0 WBh(OKBOX, 0)
#define UOKBOX0 WBh(UOKBOX, 0)
#define UOKBOXp0 WBh(UOKBOXp, 0)
// !!! These *BTN,OK*,*TOGGLE,*BUTTON blocks each hold 1 nested EVENT block */
#define OKBTN(NM,H) WBr2h(OKBTN, 1 + 2), (NM), EVENT(OK, H)
#define CANCELBTN(NM,H) WBr2h(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define CANCELBTNp(NP,H) WBr2hnf(CANCELBTN, 1 + 2), WBfield(NP), EVENT(CANCEL, H)
#define UCANCELBTN(NM,H) WBr2h(UCANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define ECANCELBTN(NM,H) WBr2h(ECANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define OKADD(NM,H) WBr2h(OKADD, 1 + 2), (NM), EVENT(CLICK, H)
#define OKTOGGLE(NM,V,H) WBr2hf(OKTOGGLE, 2 + 2), WBfield(V), (NM), \
	EVENT(CHANGE, H)
#define UTOGGLEv(NM,V,H) WBr2h(UTOGGLE, 2 + 2), &(V), (NM), EVENT(CHANGE, H)
#define BUTTON(NM,H) WBr2h(BUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define BUTTONp(NP,H) WBr2hnf(BUTTON, 1 + 2), WBfield(NP), EVENT(CLICK, H)
#define UBUTTON(NM,H) WBr2h(UBUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define EBUTTON(NM,H) WBr2h(EBUTTON, 1 + 2), (NM), EVENT(CLICK, H)
#define TLBUTTON(NM,H,X,Y) WBr2h(TLBUTTON, 2 + 2), (NM), \
	EVENT(CLICK, H), WBxyl(X, Y, 1)
#define MOUNT(V,FN,H) WBr2hf(MOUNT, 2 + 2), WBfield(V), (FN), EVENT(CHANGE, H)
#define PMOUNT(V,FN,H,K,NK) WBr2hf(PMOUNT, 4 + 2), WBfield(V), (FN), (K), \
	(void *)(NK), EVENT(CHANGE, H)
#define REMOUNTv(V) WBrh(REMOUNT, 1), &(V)
#define EXEC(FN) WBh(EXEC, 1), (FN)
#define GOTO(A) WBh(GOTO, 1), (A)
#define CALLp(V) WBhf(CALLp, 1), WBfield(V)
#define RET WBh(RET, 0)
#define IF(X) WBhf(IF, 1), WBfield(X)
#define IFx(X,N) WBhf(IF, 2), WBfield(X), (void *)(N)
#define IFv(X) WBh(IF, 1), &(X)
#define UNLESS(X) WBhf(UNLESS, 1), WBfield(X)
#define UNLESSx(X,N) WBhf(UNLESS, 2), WBfield(X), (void *)(N)
#define UNLESSv(X) WBh(UNLESS, 1), &(X)
#define UNLESSbt(V) WBh(UNLESSbt, 1), (V)
#define ENDIF(N) WBh(ENDIF, 1), (void *)(N)
#define REF(V) WBhf(REF, 1), WBfield(V)
#define REFv(V) WBh(REF, 1), &(V)
#define CLEANUP(V) WBrhf(CLEANUP, 1), WBfield(V)
#define GROUP(N) WBrh(GROUP, 1), (void *)(N)
//#define DEFGROUP WBrh(GROUP, 0)
#define IDENT(NM) WBh(IDENT, 1), (NM)
#define BORDER(T,V) WBh(BOR_##T, 1), (void *)(V)
#define DEFBORDER(T) WBh(BOR_##T, 0)
#define MKSHRINK WBh(MKSHRINK, 0)
#define NORESIZE WBh(NORESIZE, 0)
#define WANTMAX WBh(WANTMAX, 0)
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
#define ONTOP(V) WBhf(ONTOP, 1), WBfield(V)
#define ONTOP0 WBh(ONTOP, 0)
#define RAISED WBh(RAISED, 0)
#define WLIST WBh(WLIST, 0)
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
#define RTXTCOLUMND(ST,F,W,J) WBrh(RTXTCOLUMN, 3), (void *)offsetof(ST, F), \
	NULL, (void *)((W) + ((J) << 16))
#define RTXTCOLUMNDi(W,J) WBrh(RTXTCOLUMN, 3), (void *)0, \
	NULL, (void *)((W) + ((J) << 16))
#define NRTXTCOLUMND(NM,ST,F,W,J) WBrh(RTXTCOLUMN, 4), (void *)offsetof(ST, F), \
	NULL, (void *)((W) + ((J) << 16)), (NM)
#define CHKCOLUMNi0v(A,S,W,J,HC) WBr2h(CHKCOLUMNi0, 3 + 2), &(A), (void *)(S), \
	(void *)((W) + ((J) << 16)), EVENT(CHANGE, HC)
#define EVENT(T,H) WBrh(EVT_##T, 1), (H)
#define TRIGGER WBrh(TRIGGER, 0)

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

//	Extra data of LABEL
#define LABEL_VALUE	0

//	Extra data of NBOOK
#define NBOOK_TABS	0

//	Extra data of ENTRY
#define ENTRY_VALUE	0
