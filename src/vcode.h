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
	op_WINDOWpm,
	op_PAGE,
	op_TABLE,
	//
	op_VBOX,
	op_VBOXe, // VBOX'es must precede HBOX'es
	op_HBOX,
	op_TLHBOX,
	//
	op_FVBOX,
	op_SCROLL,
	op_SNBOOK,
	op_PLAINBOOK,
	op_BOOKBTN,
	op_HSEP,
	op_MLABEL,
	op_TLLABEL,
	op_TLNOSPIN,
	//
	op_SPIN,
	op_TSPIN,
	op_TLSPIN,
	//
	op_SPINa,
	op_XSPINa,
	op_TSPINa,
	//
	op_TSPINSLIDE,
	op_CHECK,
	op_TLCHECK, // must follow CHECK
	op_CHECKb,
	op_RPACK,
	op_FRPACK,
	op_RPACKd,
	op_OPT,
	op_TLOPT,
	op_PATH,
	op_PATHs,
	op_OKBOX,
	op_OKBTN,
	op_CANCELBTN,
	op_OKADD,
	op_OKNEXT,
	op_TLBUTTON,
	op_EXEC,
	op_CALLp,
	op_RET,
	op_IF,
	op_UNLESS,
	op_ENDIF,
	op_REF,
	op_MKSHRINK,
	op_NORESIZE,
	op_WANTMAX,
	op_DEFW,
	op_WPMOUSE,
	op_INSENS,
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
	op_BOR_TLLABEL,
	op_BOR_FRPACK,
	op_BOR_OKBOX,
	op_BOR_OKBTN,

	op_BOR_LAST
};

typedef void (*evt_fn)(void *ddata, void **wdata, int what, void **where);

//	Build a dialog window out of V-code decription
//void **run_create(const void **ifcode, const void *ddata, int ddsize);
void **run_create(void **ifcode, void *ddata, int ddsize);
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

#define WB_OPBITS     13
#define WB_OPMASK 0x1FFF /* ((1 << WB_OPBITS) - 1) */
#define WB_FFLAG  0x8000
#define WB_REF1   0x2000
#define WB_REF2   0x4000
#define WB_REF3   0x6000
#define WB_GETREF(X) (((X) >> WB_OPBITS) & 3)

#define WBh(NM,L) (void *)( op_##NM + ((L) << 16))
#define WBhf(NM,L) (void *)( op_##NM + ((L) << 16) + WB_FFLAG)
#define WBrh(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF1)
#define WBrhf(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF1 + WB_FFLAG)
#define WBr2h(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF2)
#define WBr2hf(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF2 + WB_FFLAG)
#define WBr3h(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF3)
#define WBr3hf(NM,L) (void *)( op_##NM + ((L) << 16) + WB_REF3 + WB_FFLAG)
#define WBfield(V) (void *)offsetof(WBbase, V)
#define WBxyl(X,Y,L) (void *)((Y) + ((X) << 8) + ((L - 1) << 16))
#define WBnh(N,H) (void *)(((H) & 255) + ((N) << 8))

#define WEND WBh(WEND, 0)
#define WSHOW WBh(WSHOW, 0)
#define WDONE WBh(WDONE, 0)
#define WINDOW(NM) WBrh(WINDOW, 1), (NM)
#define WINDOWm(NM) WBrh(WINDOWm, 1), (NM)
#define WINDOWpm(V) WBrhf(WINDOWpm, 1), WBfield(V)
#define PAGE(NM) WBh(PAGE, 1), (NM)
#define TABLE(W,H) WBh(TABLE, 1), (void *)((H) + ((W) << 16))
#define TABLE2(H) TABLE(2, (H))
#define TABLEr(W,H) WBrh(TABLE, 1), (void *)((H) + ((W) << 16))
#define HBOX WBh(HBOX, 0)
#define VBOX WBh(VBOX, 0)
#define VBOXe WBh(VBOXe, 0)
#define TLHBOXl(X,Y,L) WBh(TLHBOX, 1), WBxyl(X, Y, L)
#define FVBOX(NM) WBh(FVBOX, 1), (NM)
#define FVBOXs(NM,S) WBh(FVBOX, 2), (void *)(S), (NM)
#define SCROLL(HP,VP) WBh(SCROLL, 1), (void *)((HP) + ((VP) << 8))
#define SNBOOK WBh(SNBOOK, 0)
#define PLAINBOOK WBrh(PLAINBOOK, 0)
#define BOOKBTN(NM,V) WBhf(BOOKBTN, 2), WBfield(V), (NM)
#define HSEP WBh(HSEP, 0)
#define HSEPl(V) WBh(HSEP, 1), (void *)(V)
#define MLABEL(NM) WBh(MLABEL, 1), (NM)
#define TLLABEL(NM,X,Y) WBh(TLLABEL, 2), (NM), WBxyl(X, Y, 1)
#define TLNOSPIN(V,X,Y) WBhf(TLNOSPIN, 2), WBfield(V), WBxyl(X, Y, 1)
#define TLSPIN(V,V0,V1,X,Y) WBrhf(TLSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), WBxyl(X, Y, 1)
#define TSPIN(NM,V,V0,V1) WBrhf(TSPIN, 4), WBfield(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TSPINv(NM,V,V0,V1) WBrh(TSPIN, 4), &(V), \
	(void *)(V0), (void *)(V1), (NM)
#define TSPINa(NM,A) WBrhf(TSPINa, 2), WBfield(A), (NM)
#define SPIN(V,V0,V1) WBrhf(SPIN, 3), WBfield(V), \
	(void *)(V0), (void *)(V1)
#define SPINa(A) WBrhf(SPINa, 1), WBfield(A)
#define XSPINa(A) WBrhf(XSPINa, 1), WBfield(A)
#define TSPINSLIDE(NM,V,V0,V1) WBrhf(TSPINSLIDE, 4), \
	WBfield(V), (void *)(V0), (void *)(V1), (NM)
#define CHECK(NM,V) WBrhf(CHECK, 2), WBfield(V), (NM)
#define CHECKv(NM,V) WBrh(CHECK, 2), &(V), (NM)
#define CHECKb(NM,V,V0) WBrh(CHECKb, 3), (V), (void *)(V0), (NM)
#define TLCHECKl(NM,V,X,Y,L) WBrhf(TLCHECK, 3), WBfield(V), (NM), WBxyl(X, Y, L)
#define TLCHECK(NM,V,X,Y) TLCHECKl(NM, V, X, Y, 1)
#define TLCHECKvl(NM,V,X,Y,L) WBrh(TLCHECK, 3), &(V), (NM), WBxyl(X, Y, L)
#define TLCHECKv(NM,V,X,Y) TLCHECKvl(NM, V, X, Y, 1)
/* !!! No more than 255 choices */
#define RPACK(SS,N,H,V) WBrhf(RPACK, 3), WBfield(V), (SS), WBnh(N, H)
#define RPACKv(SS,N,H,V) WBrh(RPACK, 3), &(V), (SS), WBnh(N, H)
#define FRPACK(NM,SS,N,H,V) WBrhf(FRPACK, 4), WBfield(V), (SS), WBnh(N, H), (NM)
#define FRPACKv(NM,SS,N,H,V) WBrh(FRPACK, 4), &(V), (SS), WBnh(N, H), (NM)
#define RPACKd(SP,H,V) WBrhf(RPACKd, 3), WBfield(V), WBfield(SP), (H)
/* !!! These *OPT* blocks each hold 1 nested EVENT block */
#define OPT(SS,N,V,HS) WBr2hf(OPT, 3 + 2), WBfield(V), (SS), (void *)(N), \
	EVENT(SELECT, HS)
#define TLOPTvl(SS,N,V,HS,X,Y,L) WBr2h(TLOPT, 4 + 2), &(V), (SS), (void *)(N), \
	EVENT(SELECT, HS), WBxyl(X, Y, L)
#define PATHv(A,B,C,D) WBrh(PATH, 4), (D), (A), (B), (void *)(C)
#define PATHs(A,B,C,D) WBrh(PATHs, 4), (D), (A), (B), (void *)(C)
/* !!! This block holds 2 nested EVENT blocks */
#define OKBOX(NOK,HOK,NC,HC) WBr3h(OKBOX, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
#define OKBOX0 WBh(OKBOX, 0)
// !!! These *BTN,OK*,*BUTTON blocks each hold 1 nested EVENT block */
#define OKBTN(NM,H) WBr2h(OKBTN, 1 + 2), (NM), EVENT(OK, H)
#define CANCELBTN(NM,H) WBr2h(CANCELBTN, 1 + 2), (NM), EVENT(CANCEL, H)
#define OKADD(NM,H) WBr2h(OKADD, 1 + 2), (NM), EVENT(CLICK, H)
#define OKNEXT(NM,H) WBr2h(OKNEXT, 1 + 2), (NM), EVENT(CLICK, H)
#define TLBUTTON(NM,H,X,Y) WBr2h(TLBUTTON, 2 + 2), (NM), \
	EVENT(CLICK, H), WBxyl(X, Y, 1)
#define EXEC(FN) WBh(EXEC, 1), (FN)
#define CALLp(V) WBhf(CALLp, 1), WBfield(V)
#define RET WBh(RET, 0)
#define IF(X) WBhf(IF, 1), WBfield(X)
#define IFx(X,N) WBhf(IF, 2), WBfield(X), (void *)(N)
#define IFv(X) WBh(IF, 1), &(X)
#define UNLESS(X) WBhf(UNLESS, 1), WBfield(X)
#define UNLESSx(X,N) WBhf(UNLESS, 2), WBfield(X), (void *)(N)
#define UNLESSv(X) WBh(UNLESS, 1), &(X)
#define ENDIF(N) WBh(ENDIF, 1), (void *)(N)
#define REF(V) WBhf(REF, 1), WBfield(V)
#define BORDER(T,V) WBh(BOR_##T, 1), (void *)(V)
#define MKSHRINK WBh(MKSHRINK, 0)
#define NORESIZE WBh(NORESIZE, 0)
#define WANTMAX WBh(WANTMAX, 0)
#define DEFW(V) WBh(DEFW, 1), (void *)(V)
#define WPMOUSE WBh(WPMOUSE, 0)
#define INSENS WBh(INSENS, 0)
/* !!! Maybe better to integrate this into container codes */
//#define SETBORDER(V) WBh(SETBORDER, 1), (void *)(V)
#define EVENT(T,H) WBrh(EVT_##T, 1), (H)
#define TRIGGER WBrh(TRIGGER, 0)

//	Function to run with EXEC
typedef void **(*ext_fn)(void **r, GtkWidget ***wpp);
