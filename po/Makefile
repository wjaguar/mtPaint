include ../_conf.txt

langs = es cs fr pt pt_BR de_DE pl tr zh_TW sk

all:
	for iii in $(langs); do echo -n "$$iii = "; msgfmt -v -o $$iii.mo $$iii.po; done

install:
	for iii in $(langs); do mkdir -p $(MT_LANG_DEST)/$$iii/LC_MESSAGES/; done
	for iii in $(langs); do cp $$iii.mo $(MT_LANG_DEST)/$$iii/LC_MESSAGES/mtpaint.mo; done

uninstall:
	for iii in $(langs); do rm $(MT_LANG_DEST)/$$iii/LC_MESSAGES/mtpaint.mo; done

clean:
	rm *.mo
