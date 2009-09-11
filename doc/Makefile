include ../_conf.txt

MAN_PAGE=mtpaint.1

all:
	cp _$(MAN_PAGE) $(MAN_PAGE)
	gzip -f $(MAN_PAGE)

pod:
	pod2man "--release=$(MT_VERSION)" "--date=$(MT_DATE)" "--center=Mark Tyler's Painting Program" mtpaint.pod > _mtpaint.1

install:
	mkdir -p $(MT_MAN_DEST) $(MT_PREFIX)/share/applications $(MT_PREFIX)/share/pixmaps
	cp $(MAN_PAGE).gz $(MT_MAN_DEST)
	cp mtpaint.desktop $(MT_PREFIX)/share/applications
	cp mtpaint.png $(MT_PREFIX)/share/pixmaps

uninstall:
	rm $(MT_MAN_DEST)/$(MAN_PAGE).gz
	rm $(MT_PREFIX)/share/applications/mtpaint.desktop
	rm $(MT_PREFIX)/share/pixmaps/mtpaint.png

clean:
	rm *.gz
