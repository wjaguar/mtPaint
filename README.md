# mtPaint 3.50 - Copyright (C) 2004-2020 The Authors

See 'Credits' section for a list of the authors.

mtPaint is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.

mtPaint is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

mtPaint is a simple GTK+1/2 painting program designed for creating icons and pixel based artwork. It can edit indexed palette or 24-bit RGB images and offers basic painting and palette manipulation tools. It also has several other more powerful features such as channels, layers and animation. Due to its simplicity and lack of dependencies it runs well on GNU/Linux, Windows and older PC hardware.

There is full documentation of mtPaint's features contained in a handbook.  If you don't already have this, you can download it from the mtPaint website.

If you like mtPaint and you want to keep up to date with new releases, or you want to give some feedback, then the mailing lists may be of interest to you:

https://sourceforge.net/p/mtpaint/mailman/


## Compilation
In order to compile mtPaint on a GNU/Linux system you will need to have the libraries and headers for GTK+1 and/or GTK+2, libpng and zlib.  If you want to load or save GIF, JPEG, TIFF, JPEG2000 and WebP files you will also need giflib, libjpeg, libtiff, libopenjpeg or libjasper, and libwebp.  If you want to compile the international version you will need to have the gettext system and headers installed.  You may then adjust the Makefile/sources to cater for your needs and then:

### For GTK 2
    ./configure
    make
    su -c "make install"

### For GTK 1
    ./configure gtk1
    make
    su -c "make install"

If you want to uninstall these files from your system, you should type:

    su -c "make uninstall"

There are various configure options that may be useful for some people.  Use `./configure --help` to find out what options are available.  If you are compiling a binary for distribution to other peoples systems, the option 'asneeded' is particularly useful (if the gcc option -Wl,--as-needed is available, i.e. binutils >=2.17), as it only creates links to libraries which are absolutely necessary to mtPaint.  For example, without this option if you compile mtPaint against GTK+2.10 you will find it will not run on GTK+2.6 systems because Cairo doesn't exist on the older system.

Use `./configure release` to compile mtPaint with the same optimizations we use for distribution packages; this includes the "asneeded" option. To enable internationalization, add the "intl" option.

If you are compiling mtPaint on an older system without gtk-config, you may need to adjust the configure script so that the GTK+1 settings are done manually.  I have provided an example in the configure script to demonstrate.

You can call mtpaint with the `-v` option and the program will start in viewer mode so there will be no palette, menu bar, etc.  You can restore these items by pressing the "Home" key.  After installation you can create a symlink to add a viewer command, e.g.

    su -c "ln -s mtpaint /usr/local/bin/mtv"

Then you can open some graphics files with `mtv *.jpg`.  This is a shortcut to writing `mtpaint -v *.jpg`.  mtPaint can only edit one image at a time, but when you have more than one filename in the command line a window will appear with all of the filenames in a list.  If you select one of the names, it will be loaded.  I find this is helpful for editing several icons or digital photos.

After running mtPaint for the first time, a new file is created to store your preferred settings and previously used files etc.  This file is named ".mtpaint" and stored in the user's home directory.  If you rename or remove this file then the next time mtPaint is run it will use the default settings.

The easiest way to compile mtPaint for Windows is using MinGW cross-compiler on a GNU/Linux system, and the included "winbuild.sh" shell script. The script will compile mtPaint and all required runtime files from source code, and prepare a binary package and a separate development package, with headers and development libraries; see "gtk/README" for details.

Another alternative is doing a manual build with MinGW on GNU/Linux, for which you'll need to have installed requisite library and header files, corresponding to the runtime libraries you intend to use. Since version 3.40, the official mtPaint package for Windows uses custom-built runtime files, and development libraries and headers, produced in the automated build process described above; with version 3.31 and earlier, you could use the packages listed below for MinGW/MSYS build. Either way, after the headers and libraries are installed where MinGW expects them, you configure mtPaint for cross-compiling, then run `make` as usual. Like this:

    PATH=/usr/i586-mingw32/bin:$PATH ./configure --host=i586-mingw32 [options]
    make

It should also still be possible to compile mtPaint for Windows the old way, using MinGW/MSYS on a Windows system. However this wasn't done for quite some time, so the description below still refers to older versions of MinGW, MSYS and library packages. mtpaint.exe compiled according to it, will only be compatible with runtime libraries packaged with mtPaint 3.31 or older; to use the newer runtime (of version 3.40+), you'll need to use library and header files produced while cross-compiling the runtime (see above).

If you want to do this you must first download the mtPaint 3.31 setup program and install the files to "C:\Program Files\mtPaint\" and then:

1) Install MinGW and MSYS - http://www.mingw.org/ 
- MinGW-3.1.0-1.exe - to C:\MinGW\
- MSYS-1.0.10.exe - to C:\msys\

2) Install the GTK+2 developer packages (and dependencies like libpng) - ftp://ftp.gtk.org/pub/gtk/v2.6/win32/ and http://gnuwin32.sourceforge.net/packages.html 

For GTK+2 you will need to download and extract the following zip files to C:\msys: 
- gtk+-dev-2.6.4.zip
- pango-dev-1.8.0.zip
- atk-dev-1.9.0.zip

You will also need to download and extract the following zip files to C:\msys: 
- glib-dev-2.6.4.zip
- libpng-1.2.7-lib.zip
- zlib-1.2.1-1-lib.zip
- libungif-4.1.4-lib.zip
- jpeg-6b-3-lib.zip
- tiff-3.6.1-2-lib.zip

If you want to compile the internationlized version you will need to download and extract to C:\msys the following zip files from http://sourceforge.net/projects/gettext : 
- gettext-runtime-0.13.1.bin.woe32.zip
- gettext-tools-0.13.1.bin.woe32.zip
- libiconv-1.9.1.bin.woe32.zip

For some reason I needed to move C:\msys\bin\msgfmt & xgettext to C:\msys\local\bin\ in order to get it to run properly. If you have trouble running `msgfmt` you may need to do the same. 

3) Download the latest mtPaint sources and unpack them to C:\msys. 

4) To compile the code you must then use MSYS to `./configure`, then `make` and `make install` 

5) If all goes well, you should have mtpaint.exe which you can run using the same method described above.  You may have compiled mtPaint using more recent versions of libraries so you may need to change the filenames, such as libpng12.dll -> libpng13.dll and libungif.dll -> libungif4.dll.
 
Because I very rarely use Windows, I am sadly unable to support any other version of GTK but the one in the official package. That is, while mtPaint should in principle be able to compile and run with any version of GTK 2, only the packaged version has undergone real testing on Windows, and has been patched to fix all known Windows-specific bugs in it.

## Credits

mtPaint is maintained by Dmitry Groshev.

wjaguar@users.sourceforge.net
http://mtpaint.sourceforge.net/

The following people (in alphabetical order) have contributed directly to the project, and are therefore worthy of gracious thanks for their generosity and hard work:


### Authors

- Dmitry Groshev - Contributing developer for version 2.30. Lead developer and maintainer from version 3.00 to the present.
- Mark Tyler - Original author and maintainer up to version 3.00, occasional contributor thereafter.
- Xiaolin Wu - Wrote the Wu quantizing method - see wu.c for more information.


General Contributions (Feedback and Ideas for improvements unless otherwise stated)

- Abdulla Al Muhairi - Website redesign April 2005
- Alan Horkan
- Alexandre Prokoudine
- Antonio Andrea Bianco
- Charlie Ledocq
- Dennis Lee
- Donald White
- Ed Jason
- Eddie Kohler - Created Gifsicle which is needed for the creation and viewing of animated GIF files http://www.lcdf.org/gifsicle/
- Guadalinex Team (Junta de Andalucia) - man page, Launchpad/Rosetta registration
- Lou Afonso
- Magnus Hjorth
- Martin Zelaia
- Pasi Kallinen
- Pavel Ruzicka
- Puppy Linux (Barry Kauler)
- Victor Copovi
- Vlastimil Krejcir
- William Kern


### Translations

- Brazilian Portuguese - Paulo Trevizan, Valter Nazianzeno
- Czech - Pavel Ruzicka, Martin Petricek, Roman Hornik
- Dutch - Hans Strijards
- French - Nicolas Velin, Pascal Billard, Sylvain Cresto, Johan Serre, Philippe Etienne
- Galician - Miguel Anxo Bouzada
- German - Oliver Frommel, B. Clausius, Ulrich Ringel
- Hungarian - Ur Balazs
- Italian - Angelo Gemmi
- Japanese - Norihiro YONEDA
- Polish - Bartosz Kaszubowski, LucaS
- Portuguese - Israel G. Lugo, Tiago Silva
- Russian - Sergey Irupin, Dmitry Groshev
- Simplified Chinese - Cecc
- Slovak - Jozef Riha
- Spanish - Guadalinex Team (Junta de Andalucia), Antonio Sanchez Leon, Miguel Anxo Bouzada, Francisco Jose Rey, Adolfo Jayme
- Swedish - Daniel Nylander, Daniel Eriksson
- Tagalog - Anjelo delCarmen
- Taiwanese Chinese - Wei-Lun Chao
- Turkish - Muhammet Kara, Tutku Dalmaz

