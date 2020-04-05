include ../_conf.txt

MAN_PAGE=mtpaint.1

all: $(MAN_PAGE)

$(MAN_PAGE): mtpaint.pod
	pod2man "--release=$(MT_VERSION)" "--date=$(MT_DATE)" "--center=Mark Tyler's Painting Program" mtpaint.pod > $(MAN_PAGE)

html: vcode.html

%.html: %.t2t
	txt2tags -t html $*.t2t

install:
	mkdir -p $(DESTDIR)$(MT_MAN_DEST)/man1 $(DESTDIR)$(MT_DATAROOT)/applications $(DESTDIR)$(MT_DATAROOT)/pixmaps
	cp $(MAN_PAGE) $(DESTDIR)$(MT_MAN_DEST)/man1
	cp mtpaint.desktop $(DESTDIR)$(MT_DATAROOT)/applications
	cp mtpaint.png $(DESTDIR)$(MT_DATAROOT)/pixmaps

uninstall:
	rm $(DESTDIR)$(MT_MAN_DEST)/man1/$(MAN_PAGE)
	rm $(DESTDIR)$(MT_DATAROOT)/applications/mtpaint.desktop
	rm $(DESTDIR)$(MT_DATAROOT)/pixmaps/mtpaint.png
