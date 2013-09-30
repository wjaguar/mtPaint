/*	vcode.h
	Copyright (C) 2013 Dmitry Groshev

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
	op_WDONE,
	op_WINDOW,
	op_WINDOWm,
	op_PAGE,
	op_TABLE,
	op_TABLEr,
	op_HBOX,
	op_TLHBOX,
	op_SCROLL,
	op_SNBOOK,
	op_HSEP,
	op_TLLABEL,
	op_TSPIN,
	op_TSPINv,
	op_TSPINa,
	op_SPIN,
	op_XSPINa,
	op_TSPINSLIDE,
	op_CHECK,
	op_CHECKv,
	op_CHECKb,
	op_TLCHECK,
	op_TLCHECKv,
	op_RPACK,
	op_RPACKv,
	op_TLOPTv,
	op_PATHv,
	op_PATHs,
	op_OKBOX,
	op_OKBTN,
	op_CANCELBTN,
	op_OKADD,
	op_OKNEXT,
	op_EXEC,
	op_IF,
	op_IFv,
	op_UNLESS,
	op_UNLESSv,
	op_ENDIF,
	op_REF,
	op_MKSHRINK,
	op_NORESIZE,
	op_WANTMAX,
	op_DEFW,
	op_WPMOUSE,
//	op_SETBORDER,

	op_EVT_0,
	op_EVT_OK = op_EVT_0,
	op_EVT_CANCEL,
	op_EVT_CLICK,
	op_EVT_SELECT,
	op_EVT_CHANGE,

	op_EVT_LAST,
	op_TRIGGER = op_EVT_LAST,

	op_BOR_0,
	op_BOR_TABLE = op_BOR_0,
	op_BOR_TSPIN,
	op_BOR_OKBOX,
	op_BOR_OKBTN,

	op_BOR_LAST
};

typedef void (*evt_fn)(void *ddata, void **wdata, int what, void **where);

//	Build a dialog window out of bytecode decription
void **run_create(void **ifcode, int ifsize, void *ddata, int ddsize);
//	Query dialog contents using its widget-map
void run_query(void **wdata);
//	Destroy a dialog by its widget-map
#define run_destroy(W) destroy_dialog(GET_REAL_WINDOW(W))

//	Extract data structure out of widget-map
#define GET_DDATA(V) ((V)[0])
//	Extract toplevel window slot out of widget-map
#define GET_WINDOW(V) ((V) + 1)
//	Extract actual toplevel window out of widget-map
#define GET_REAL_WINDOW(V) ((V)[1])

//	Set sensitive state on slot
void cmd_sensitive(void **slot, int state);
//	Set visible state on slot
void cmd_showhide(void **slot, int state);
//	Set value on spinslider slot
void cmd_set(void **slot, int v);
//	Set value & limits on spinslider slot
void cmd_set3(void **slot, int *v);
//	Read back slot value (as is), return its storage location
void *cmd_read(void **slot, void *ddata);

#define WBh(NM,L) (void *)( op_##NM + ((L) << 16))
#define WBfield(V) (void *)offsetof(WBbase, V)
#define WBxyl(X,Y,L) (void *)((Y) + ((X) << 8) + ((L - 1) << 16))

#define WEND WBh(WEND, 0)
#define WSHOW WBh(WSHOW, 0)
#define WDONE WBh(WDONE, 0)
#define WINDOW(NM) WBh(WINDOW, 1), (NM)
#define WINDOWm(NM) WBh(WINDOWm, 1), (NM)
#define PAGE(NM) WBh(PAGE, 1), (NM)
#define TABLE(W,H) WBh(TABLE, 1), (void *)((H) + ((W) << 16))
#define TABLE2(H) TABLE(2, (H))
#define TABLEr(W,H) WBh(TABLEr, 1), (void *)((H) + ((W) << 16))
#define HBOX WBh(HBOX, 0)
#define TLHBOXl(X,Y,L) WBh(TLHBOX, 1), WBxyl(X, Y, L)
#define SCROLL(HP,VP) WBh(SCROLL, 1), (void *)((HP) + ((VP) << 8))
#define SNBOOK WBh(SNBOOK, 0)
#define HSEP WBh(HSEP, 0)
#define HSEPl(V) WBh(HSEP, 1), (void *)(V)
#define TLLABEL(NM,X,Y) WBh(TLLABEL, 2), (NM), WBxyl(X, Y, 1)
#define TSPIN(NM,V,V0,V1) WBh(TSPIN, 4), (NM), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define TSPINv(NM,V,V0,V1) WBh(TSPINv, 4), (NM), &(V), \
	(void *)(V0), (void *)(V1)
#define TSPINa(NM,A) WBh(TSPINa, 2), (NM), WBfield(A)
#define SPIN(V,V0,V1) WBh(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define XSPINa(A) WBh(XSPINa, 1), WBfield(A)
#define TSPINSLIDE(NM,V,V0,V1) WBh(TSPINSLIDE, 4), (NM), \
	WBfield(V), (void *)(V0), (void *)(V1)
#define CHECK(NM,V) WBh(CHECK, 2), (NM), WBfield(V)
#define CHECKv(NM,V) WBh(CHECKv, 2), (NM), &(V)
#define CHECKb(NM,V,V0) WBh(CHECKb, 3), (NM), (V), (void *)(V0)
#define TLCHECKl(NM,V,X,Y,L) WBh(TLCHECK, 3), (NM), WBfield(V), WBxyl(X, Y, L)
#define TLCHECK(NM,V,X,Y) TLCHECKl(NM, V, X, Y, 1)
#define TLCHECKvl(NM,V,X,Y,L) WBh(TLCHECKv, 3), (NM), &(V), WBxyl(X, Y, L)
#define TLCHECKv(NM,V,X,Y) TLCHECKvl(NM, V, X, Y, 1)
/* !!! No more than 255 choices */
#define RPACK(SS,N,H,V) WBh(RPACK, 3), (SS), \
	(void *)(((N) & 255) + ((H) << 8)), WBfield(V)
#define RPACKv(SS,N,H,V) WBh(RPACKv, 3), (SS), \
	(void *)(((N) & 255) + ((H) << 8)), &(V)
/* !!! This block holds 1 nested EVENT block */
#define TLOPTvl(SS,N,V,HS,X,Y,L) WBh(TLOPTv, 4 + 2), (SS), (void *)(N), \
	&(V), EVENT(SELECT, HS), WBxyl(X, Y, L)
#define PATHv(A,B,C,D) WBh(PATHv, 4), (A), (B), (void *)(C), (D)
#define PATHs(A,B,C,D) WBh(PATHs, 4), (A), (B), (void *)(C), (D)
/* !!! This block holds 2 nested EVENT blocks */
#define OKBOX(NOK,HOK,NC,HC) WBh(OKBOX, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
#define OKBOX0 WBh(OKBOX, 0)
/* !!! These *BTN/OK* blocks each hold 1 nested EVENT block */
#define OKBTN(NM,H) WBh(OKBTN, 1 + 2), (NM), EVENT(OK, H)
#define CANCELBTN(NM,H) WBh(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define OKADD(NM,H) WBh(OKADD, 1 + 2), (NM), EVENT(CLICK, H)
#define OKNEXT(NM,H) WBh(OKNEXT, 1 + 2), (NM), EVENT(CLICK, H)
#define EXEC(FN) WBh(EXEC, 1), (FN)
#define IF(X) WBh(IF, 1), WBfield(X)
#define IFx(X,N) WBh(IF, 2), WBfield(X), (void *)(N)
#define IFv(X) WBh(IFv, 1), &(X)
#define UNLESS(X) WBh(UNLESS, 1), WBfield(X)
#define UNLESSv(X) WBh(UNLESSv, 1), &(X)
#define ENDIF(N) WBh(ENDIF, 1), (void *)(N)
#define REF(V) WBh(REF, 1), WBfield(V)
#define BORDER(T,V) WBh(BOR_##T, 1), (void *)(V)
#define MKSHRINK WBh(MKSHRINK, 0)
#define NORESIZE WBh(NORESIZE, 0)
#define WANTMAX WBh(WANTMAX, 0)
#define DEFW(V) WBh(DEFW, 1), (void *)(V)
#define WPMOUSE WBh(WPMOUSE, 0)
/* !!! Maybe better to integrate this into container codes */
//#define SETBORDER(V) WBh(SETBORDER, 1), (void *)(V)
#define EVENT(T,H) WBh(EVT_##T, 1), (H)
#define TRIGGER WBh(TRIGGER, 0)

//	Function to run with EXEC
typedef void **(*ext_fn)(void **r, GtkWidget ***wpp);
