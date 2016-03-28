include ../_conf.txt

# Generate identical object files if possible
CFLAGS = $(CFLAG) $(SET_RANDSEED)
LDFLAGS = $(LDFLAG)

BIN = mtpaint$(EXEEXT)

OBJS = mainwindow.o inifile.o png.o memory.o canvas.o otherwindow.o mygtk.o\
	viewer.o polygon.o layer.o info.o wu.o prefs.o ani.o mtlib.o\
	toolbar.o channels.o csel.o shifter.o spawn.o font.o fpick.o icons.o\
	cpick.o thread.o vcode.o

$(BIN): main.o $(OBJS)
	$(CC) main.o $(OBJS) -o $(BIN) $(LDFLAGS)

$(OBJS): *.h graphics/*

.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $*.c

clean:
	rm -f *.o $(BIN)* $(LIBNAME) $(LIBNAME2) $(SLIBNAME)

install:
	mkdir -p $(DESTDIR)$(BIN_INSTALL)
	cp $(BIN) $(DESTDIR)$(BIN_INSTALL)

uninstall:
	rm $(DESTDIR)$(BIN_INSTALL)/$(BIN)
