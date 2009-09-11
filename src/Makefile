include ../_conf.txt

CFLAGS = $(CFLAG)
LDFLAGS = $(LDFLAG) $(GLIB_LIB)

BIN = mtpaint
LIBNAME = libmtpaint.so.0
LIBNAME2 = libmtpaint.so
SLIBNAME = libmtpaint.a
INCNAME = mtpaint.h
INCINSTALL = /usr/include
LIBINSTALL = /usr/lib

OBJS = mainwindow.o inifile.o png.o memory.o canvas.o otherwindow.o mygtk.o\
	viewer.o quantizer.o polygon.o layer.o info.o wu.o prefs.o ani.o mtlib.o\
	toolbar.o channels.o csel.o shifter.o spawn.o

$(BIN): main.o $(OBJS)
	$(CC) main.o $(OBJS) -o $(BIN) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $*.c

clean:
	rm -f *.o $(BIN)* $(LIBNAME) $(LIBNAME2) $(SLIBNAME)

install:
	mkdir -p $(BIN_INSTALL)
	cp $(BIN) $(BIN_INSTALL)

uninstall:
	rm $(BIN_INSTALL)/$(BIN)

libs:	main.o api.o $(OBJS)
	$(CC) api.o $(OBJS) -o $(LIBNAME) $(LDFLAGS) -shared -Wl,-soname,$(LIBNAME)
	ar rc $(SLIBNAME) api.o $(OBJS) $(MTK_LIB)
	ranlib $(SLIBNAME)
	ln -sf $(LIBNAME) $(LIBNAME2)
	$(CC) main.o -o $(BIN) $(LIBNAME) $(LDFLAGS)

libinstall:
	cp api.h $(INCINSTALL)/$(INCNAME)
	cp -d $(LIBNAME) $(LIBNAME2) $(SLIBNAME) $(LIBINSTALL)
#	cp $(BIN) $(BIN_INSTALL)
	ldconfig
