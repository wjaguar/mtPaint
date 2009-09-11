include ../_conf.txt

CFLAGS = $(CFLAG)
LDFLAGS = $(LDFLAG) -lpng -lz $(GLIB_LIB)

BIN = mtpaint

OBJS = main.o mainwindow.o inifile.o png.o memory.o canvas.o otherwindow.o mygtk.o viewer.o quantizer.o polygon.o layer.o info.o wu.o prefs.o ani.o mtlib.o

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $(BIN) $(LDFLAGS)

clean:
	rm -f *.o $(BIN)*

install:
	mkdir -p $(BIN_INSTALL)
	cp $(BIN) $(BIN_INSTALL)

uninstall:
	rm $(BIN_INSTALL)/$(BIN)
