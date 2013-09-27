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
	op_TABLE2,
	op_SCROLL,
	op_SNBOOK,
	op_HSEP,
	op_TSPIN,
	op_TSPINv,
	op_TSPINa,
	op_SPIN,
	op_CHECK,
	op_CHECKv,
	op_CHECKb,
	op_RPACK,
	op_RPACKv,
	op_PATHv,
	op_PATHs,
	op_OKBOX,
	op_OKADD,
	op_EVT_OK,
	op_EVT_CANCEL,
	op_EVT_CLICK,
	op_EXEC,
	op_IF,
	op_UNLESS,
	op_MKSHRINK,
	op_WANTMAX,
	op_DEFW,

	op_BOR_0,
	op_BOR_TABLE = op_BOR_0,
	op_BOR_TSPIN,
	op_BOR_OKBOX,

	op_BOR_LAST
};

typedef void (*evt_fn)(void *ddata, void **wdata, int what, void **where);

//	Build a dialog window out of bytecode decription
void **run_create(void **ifcode, int ifsize, void *ddata, int ddsize);
//	Query dialog contents using its widget-map
void run_query(void **wdata);

//	Extract toplevel window out of widget-map
#define GET_WINDOW(V) ((V)[1])
//	Extract data structure out of widget-map
#define GET_DDATA(V) ((V)[0])

#define WBh(NM,L) (void *)( op_##NM + ((L) << 16))

#define WEND WBh(WEND, 0)
#define WSHOW WBh(WSHOW, 0)
#define WDONE WBh(WDONE, 0)
#define WINDOW(NM) WBh(WINDOW, 1), (NM)
#define WINDOWm(NM) WBh(WINDOWm, 1), (NM)
#define PAGE(NM) WBh(PAGE, 1), (NM)
#define TABLE2(H) WBh(TABLE2, 1), (void *)(H)
#define SCROLL(HP,VP) WBh(SCROLL, 1), (void *)((HP) + ((VP) << 8))
#define SNBOOK WBh(SNBOOK, 0)
#define HSEP WBh(HSEP, 0)
#define HSEPl(V) WBh(HSEP, 1), (void *)(V)
#define TSPIN(NM,V,V0,V1) WBh(TSPIN, 4), (NM), (void *)offsetof(WBbase, V), \
	(void *)(V0), (void *)(V1)
#define TSPINv(NM,V,V0,V1) WBh(TSPINv, 4), (NM), &(V), \
	(void *)(V0), (void *)(V1)
#define TSPINa(NM,A) WBh(TSPINa, 2), (NM), (void *)offsetof(WBbase, A)
#define SPIN(V,V0,V1) WBh(SPIN, 3), (void *)offsetof(WBbase, V), \
	(void *)(V0), (void *)(V1)
#define CHECK(NM,V) WBh(CHECK, 2), (NM), (void *)offsetof(WBbase, V)
#define CHECKv(NM,V) WBh(CHECKv, 2), (NM), &(V)
#define CHECKb(NM,V,V0) WBh(CHECKb, 3), (NM), (V), (void *)(V0)
/* !!! No more than 255 choices */
#define RPACK(SS,N,H,V) WBh(RPACK, 3), (SS), \
	(void *)(((N) & 255) + ((H) << 8)), (void *)offsetof(WBbase, V)
#define RPACKv(SS,N,H,V) WBh(RPACKv, 3), (SS), \
	(void *)(((N) & 255) + ((H) << 8)), &(V)
#define PATHv(A,B,C,D) WBh(PATHv, 4), (A), (B), (void *)(C), (D)
#define PATHs(A,B,C,D) WBh(PATHs, 4), (A), (B), (void *)(C), (D)
/* !!! This block holds 2 nested EVENT blocks */
#define OKBOX(NOK,HOK,NC,HC) WBh(OKBOX, 2 + 2 * 2), (NOK), (NC), \
	EVENT(OK, HOK), EVENT(CANCEL, HC)
/* !!! This block holds 1 nested EVENT block */
#define OKADD(NM,H) WBh(OKADD, 1 + 2), (NM), EVENT(CLICK, H)
#define EVENT(T,H) WBh(EVT_##T, 1), (H)
#define EXEC(FN) WBh(EXEC, 1), (FN)
#define IF(X) WBh(IF, 1), (void *)offsetof(WBbase, X)
#define UNLESS(X) WBh(UNLESS, 1), (void *)offsetof(WBbase, X)
#define BORDER(T,V) WBh(BOR_##T, 1), (void *)(V)
#define MKSHRINK WBh(MKSHRINK, 0)
#define WANTMAX WBh(WANTMAX, 0)
#define DEFW(V) WBh(DEFW, 1), (void *)(V)

//	Function to run with EXEC
typedef void **(*ext_fn)(void **r, GtkWidget ***wpp);
