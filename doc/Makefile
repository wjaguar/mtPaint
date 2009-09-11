include ../_conf.txt

MAN_PAGE=mtpaint.1

all:
	cp _$(MAN_PAGE) $(MAN_PAGE)
	gzip -f $(MAN_PAGE)

pod:
	pod2man "--release=$(MT_VERSION)" "--date=$(MT_DATE)" "--center=Mark Tyler's Painting Program" mtpaint.pod > _mtpaint.1

install:
	mkdir -p $(MT_MAN_DEST)
	cp $(MAN_PAGE).gz $(MT_MAN_DEST)

uninstall:
	rm $(MT_MAN_DEST)/$(MAN_PAGE).gz

clean:
	rm *.gz
