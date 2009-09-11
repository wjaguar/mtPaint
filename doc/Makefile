include ../_conf.txt

MAN_PAGE=mtpaint.1

all:
	cp _$(MAN_PAGE) $(MAN_PAGE)
	gzip -f $(MAN_PAGE)

pod:
	pod2man "--release=$(MT_VERSION)" "--date=$(MT_DATE)" "--center=Mark Tyler's Painting Program" mtpaint.pod > _mtpaint.1

install:
	mkdir -p $(DESTDIR)$(MT_MAN_DEST) $(DESTDIR)$(MT_PREFIX)/share/applications $(DESTDIR)$(MT_PREFIX)/share/pixmaps
	cp $(MAN_PAGE).gz $(DESTDIR)$(MT_MAN_DEST)
	cp mtpaint.desktop $(DESTDIR)$(MT_PREFIX)/share/applications
	cp mtpaint.png $(DESTDIR)$(MT_PREFIX)/share/pixmaps

uninstall:
	rm $(DESTDIR)$(MT_MAN_DEST)/$(MAN_PAGE).gz
	rm $(DESTDIR)$(MT_PREFIX)/share/applications/mtpaint.desktop
	rm $(DESTDIR)$(MT_PREFIX)/share/pixmaps/mtpaint.png

clean:
	rm *.gz
