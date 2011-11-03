#!/bin/sh
# winbuild.sh - cross-compile GTK+ and its dependencies for Windows

# Copyright (C) 2010,2011 Dmitry Groshev

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program in the file COPYING.


##########################
# CONFIGURATION SETTINGS #
##########################

LIBS="giflib zlib libjpeg libpng libtiff freetype jasper lcms "\
"libiconv gettext glib atk pango gtk"
PROGRAMS="gifsicle mtpaint"
PHONY="libs all"
LOCALES="es cs fr pt pt_BR de pl tr zh_TW sk zh_CN ja ru gl nl it sv tl"
# Applied only to GTK+ ATM
OPTIMIZE="-march=i686 -O2 -fweb -fomit-frame-pointer -fmodulo-sched -Wno-pointer-sign"

# Everything works OK with package-relative prefix, for now
WPREFIX=

SRCDIR=`pwd`
TOPDIR="$SRCDIR/zad"

# Directories for various parts
WRKDIR="$TOPDIR/wrk" # Where sources are compiled
INSDIR="$TOPDIR/ins" # Where files get installed
PKGDIR="$TOPDIR/pkg" # Where package gets formed
DEVDIR="$TOPDIR/dev" # Where dev files get collected
# Also "$TOPDIR/bin", "$TOPDIR/include" & "$TOPDIR/lib", with symlinks to actual files
UNPDIR="$WRKDIR/_000_" # Where archives are unpacked

########################
# CROSS-COMPILER PATHS #
########################

# SpeedBlue cross-MinGW or similar
# http://www.speedblue.org/cross_compilation/
MPREFIX=/usr/i586-mingw32-4.2.4
MTARGET=i586-mingw32

# mingw-cross-env
# http://mingw-cross-env.nongnu.org/
#MPREFIX=~/mingw-cross-env-2.18/usr
#MTARGET=i686-pc-mingw32

LONGPATH="$TOPDIR:$MPREFIX/bin:$PATH"
SHORTPATH="$TOPDIR:$TOPDIR/bin:$PATH"
ALLPATH="$TOPDIR:$TOPDIR/bin:$MPREFIX/bin:$PATH"

########################

# Initialize vars
COMPONENTS=" $LIBS $PROGRAMS $PHONY "
for ZAD in $COMPONENTS
do
	eval "DEP_$ZAD="
	eval "NEED_$ZAD="
	eval "HAVE_$ZAD="
done
REBUILD= # Don't recompile by default

INNER_DIRS=" bin include lib "
# !!! Parameter shouldn't contain directory
UNPACK ()
{
	local ZAD
	local CNT
	local FFILE
	local FNAME
	local FCMD

	# Identify the source archive
	for ZAD in "$SRCDIR"/$1
	do
		if [ -f "$ZAD" ]
		then
			FFILE="$ZAD"
			break
		fi
	done
	if [ -z "$FFILE" ]
	then
		echo "ERROR: $1 not found in $SRCDIR"
		exit 1
	fi

	# Prepare to unpack
	FNAME="${FFILE#$SRCDIR/}"
	case "$FFILE" in
	*.tar.bz2) FDIR=.tar.bz2 ; FCMD="tar $2 -xf" ;;
	*.tar.gz)  FDIR=.tar.gz ; FCMD="tar $2 -xf" ;;
	*.zip)     FDIR=.zip ; FCMD="unzip $2" ;;
	*)         echo "ERROR: $FNAME unknown archive type" ; exit 1 ;;
	esac
	FDIR="$WRKDIR/${FNAME%$FDIR}"
	DESTDIR="$INSDIR/${FDIR##*/}"
	DEST="$DESTDIR$WPREFIX"

	[ -d "$DESTDIR" -a ! "$REBUILD" ] && return 1

	# Prepare temp dir & unpack
	rm -rf "$UNPDIR"
	mkdir -p "$UNPDIR"
	cd "$UNPDIR"
	$FCMD "$FFILE"
	cd "$WRKDIR"

	# Find out if tarbombing happened
	CNT=0
	for ZAD in "$UNPDIR"/*
	do
		CNT=$((CNT+1))
	done
	# Check for unexpanded glob
	if [ $CNT = 1 ] && [ ! -e "$ZAD" ]
	then
		echo "ERROR: empty archive $FNAME"
		exit 1
	fi

	# Move the files to regular location
	rm -rf "$FDIR" "$DESTDIR"
	if [ $CNT = 1 ] && [ -d "$ZAD" ] && \
		[ "${INNER_DIRS%${ZAD##*/} *}" = "$INNER_DIRS" ]
	then # With enclosing directory
		mv "$ZAD/" "$FDIR"
	else # Tarbomb
		mv "$UNPDIR" "$FDIR"
	fi
	cd "$FDIR"
# On return, source directory is current and FDIR holds it;
# DESTDIR holds installation directory; and DEST , installdir+prefix
	return 0
}

# Hardcoded direction - from current directory to DEST
COPY_BINARIES ()
{
	mkdir -p "$DEST"
	cp -R ./ "$DEST"
	chmod -R a-x,a+X "$DEST"
}

# Hardcoded origin - DEST
EXPORT ()
{
	cp -sfRT "$DEST/include/" "$TOPDIR/include/"
	cp -sfRT "$DEST/lib/" "$TOPDIR/lib/"
	rm -f "$TOPDIR/lib/"*.la # !!! These break some utils' compilation
}

# Tools
TARGET_STRIP="$MPREFIX/bin/$MTARGET-strip"

BUILD_giflib ()
{
	UNPACK "giflib-*.tar.*" || return 0
	PATH="$LONGPATH"
	./configure CPPFLAGS=-D_OPEN_BINARY LDFLAGS=-no-undefined \
		--prefix="$WPREFIX" --host=$MTARGET
	make all
	make install-strip DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
	EXPORT
}

BUILD_zlib ()
{
	UNPACK "zlib-*.tar.*" || return 0
	PATH="$SHORTPATH"
	make -f win32/Makefile.gcc install INCLUDE_PATH="$DEST/include" \
		LIBRARY_PATH="$DEST/lib"
	mkdir -p "$DEST/bin"
	cp -fp zlib1.dll "$DEST"/bin
	cp -fp libzdll.a "$DEST"/lib/libz.dll.a
	EXPORT
}

BUILD_libjpeg ()
{
	UNPACK "jpegsrc.*.tar.*" || return 0
	PATH="$LONGPATH"
	./configure --prefix="$WPREFIX" --host=$MTARGET
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_libpng="zlib"
BUILD_libpng ()
{
	UNPACK "libpng-*.tar.*" || return 0
	# !!! Binutils 2.20 cannot create proper export libs if given a version
	# script; so any libpng version before 1.4.4 needs the below fix
	sed -i 's/have_ld_version_script=yes/have_ld_version_script=no/' ./configure
	# !!! Make libpng12 default DLL, same as it is default everything else
	sed -i 's/for ext in a la so sl/for ext in a dll.a la so sl/' ./Makefile.in
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --without-libpng-compat
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_libtiff="zlib libjpeg"
BUILD_libtiff ()
{
	UNPACK "tiff-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-cxx
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_freetype="zlib"
BUILD_freetype ()
{
	UNPACK "freetype-*.tar.*" || return 0
# !!! Cross-compile breaks down if cross-gcc named "gcc" is present in PATH
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET
	make
	"$TARGET_STRIP" --strip-unneeded objs/.libs/*.dll objs/.libs/*.a
	make install DESTDIR="$DESTDIR"
	EXPORT
}

DEP_jasper="libjpeg"
BUILD_jasper ()
{
	UNPACK "jasper-*.zip" || return 0
	patch -p1 <<- 'END' # Fix CVE-2007-2721
	--- jasper-1.900.1.orig/src/libjasper/jpc/jpc_cs.c	2007-01-19 22:43:07.000000000 +0100
	+++ jasper-1.900.1/src/libjasper/jpc/jpc_cs.c	2007-04-06 01:29:02.000000000 +0200
	@@ -982,7 +982,10 @@ static int jpc_qcx_getcompparms(jpc_qcxc
	 		compparms->numstepsizes = (len - n) / 2;
	 		break;
	 	}
	-	if (compparms->numstepsizes > 0) {
	+	if (compparms->numstepsizes > 3 * JPC_MAXRLVLS + 1) {
	+		jpc_qcx_destroycompparms(compparms);
	+		return -1;
	+	} else if (compparms->numstepsizes > 0) {
	 		compparms->stepsizes = jas_malloc(compparms->numstepsizes *
	 		  sizeof(uint_fast16_t));
	 		assert(compparms->stepsizes);
	END
	patch -p1 <<- 'END' # Fix compilation
	diff -rup jasper-1.900.1/src/appl/tmrdemo.c jasper-1.900.1.new/src/appl/tmrdemo.c
	--- jasper-1.900.1/src/appl/tmrdemo.c	2007-01-19 16:43:08.000000000 -0500
	+++ jasper-1.900.1.new/src/appl/tmrdemo.c	2008-09-09 09:14:21.000000000 -0400
	@@ -1,4 +1,5 @@
	 #include <jasper/jasper.h>
	+#include <windows.h>
	 
	 int main(int argc, char **argv)
	 {
	@@ -43,7 +44,7 @@ int main(int argc, char **argv)
	 	printf("zero time %.3f us\n", t * 1e6);
	 
	 	jas_tmr_start(&tmr);
	-	sleep(1);
	+	Sleep(1);
	 	jas_tmr_stop(&tmr);
	 	t = jas_tmr_get(&tmr);
	 	printf("time delay %.8f s\n", t);
	END
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-no-undefined -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-shared \
		lt_cv_deplibs_check_method='match_pattern \.dll\.a$'
	# !!! Otherwise it tests for PE file format, which naturally fails
	# if no .la file is present to provide redirection
	make
	make install-strip DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/libjasper*.dll
	EXPORT
}

BUILD_lcms ()
{
	UNPACK "lcms2-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

# These were extracting prebuilt packages, instead of compiling from sources
: << 'COMMENTED_OUT'
BUILD_libiconv ()
{
	UNPACK "libiconv-1.9.1.bin.woe32.zip" || return 0
	COPY_BINARIES
	local ZAD
	local PERED
	for ZAD in "$DEST"/lib/*.lib
	do
		PERED="${ZAD##*/}"
		mv -f "$ZAD" "${ZAD%$PERED}"lib"${PERED%.lib}".dll.a
	done
	EXPORT
}

BUILD_gettext ()
{
	UNPACK "gettext-0.14.5.zip" && COPY_BINARIES
	UNPACK "gettext-dev-0.14.5.zip" || return 0
	mkdir -p "$DEST/include" "$DEST/lib"
	cp -fp include/libintl.h "$DEST"/include
	cp -fp lib/libintl.a "$DEST"/lib
	cp -fp lib/intl.lib "$DEST"/lib/libintl.dll.a
	chmod -R a-x,a+X "$DEST"
	EXPORT
}

BUILD_glib ()
{
	UNPACK "glib-2.6.*.zip" && COPY_BINARIES
	UNPACK "glib-dev-2.6.*.zip" || return 0
	COPY_BINARIES
	rm -f "$DEST"/lib/*.def "$DEST"/lib/*.lib
	EXPORT
}

BUILD_atk ()
{
	UNPACK "atk-1.9.0.zip" && COPY_BINARIES
	UNPACK "atk-dev-1.9.0.zip" || return 0
	COPY_BINARIES
	rm -f "$DEST"/lib/*.lib
	EXPORT
}

BUILD_pango ()
{
	UNPACK "pango-1.8.*.zip" && COPY_BINARIES
	UNPACK "pango-dev-1.8.*.zip" || return 0
	COPY_BINARIES
	rm -f "$DEST"/lib/*.def "$DEST"/lib/*.lib
	EXPORT
}
COMMENTED_OUT

BUILD_libiconv ()
{
	UNPACK "libiconv-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-static
	make
	make install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*
	# For libiconv 1.9.2, first ever to have support for MinGW
	mv "$DEST"/bin/iconv "$DEST"/bin/iconv.exe || true
	EXPORT
}

DEP_gettext="libiconv"
BUILD_gettext ()
{
	UNPACK "gettext-0.14.*.tar.*" || return 0
	# git ec56ad8f2bf513a88efc3dba44953edff3909207
	# Fix some incorrect SUBLANG_* values that were taken from glib's gwin32.c.
	patch -p1 <<- 'END' # Is needed to fix build failure with w32api 3.15+
	--- a/gettext-runtime/intl/localename.c
	+++ b/gettext-runtime/intl/localename.c
	@@ -494,10 +494,10 @@
	 # define SUBLANG_AZERI_CYRILLIC 0x02
	 # endif
	 # ifndef SUBLANG_BENGALI_INDIA
	-# define SUBLANG_BENGALI_INDIA 0x00
	+# define SUBLANG_BENGALI_INDIA 0x01
	 # endif
	 # ifndef SUBLANG_BENGALI_BANGLADESH
	-# define SUBLANG_BENGALI_BANGLADESH 0x01
	+# define SUBLANG_BENGALI_BANGLADESH 0x02
	 # endif
	 # ifndef SUBLANG_CHINESE_MACAU
	 # define SUBLANG_CHINESE_MACAU 0x05
	@@ -590,16 +590,16 @@
	 # define SUBLANG_NEPALI_INDIA 0x02
	 # endif
	 # ifndef SUBLANG_PUNJABI_INDIA
	-# define SUBLANG_PUNJABI_INDIA 0x00
	+# define SUBLANG_PUNJABI_INDIA 0x01
	 # endif
	 # ifndef SUBLANG_PUNJABI_PAKISTAN
	-# define SUBLANG_PUNJABI_PAKISTAN 0x01
	+# define SUBLANG_PUNJABI_PAKISTAN 0x02
	 # endif
	 # ifndef SUBLANG_ROMANIAN_ROMANIA
	-# define SUBLANG_ROMANIAN_ROMANIA 0x00
	+# define SUBLANG_ROMANIAN_ROMANIA 0x01
	 # endif
	 # ifndef SUBLANG_ROMANIAN_MOLDOVA
	-# define SUBLANG_ROMANIAN_MOLDOVA 0x01
	+# define SUBLANG_ROMANIAN_MOLDOVA 0x02
	 # endif
	 # ifndef SUBLANG_SERBIAN_LATIN
	 # define SUBLANG_SERBIAN_LATIN 0x02
	@@ -607,12 +607,12 @@
	 # ifndef SUBLANG_SERBIAN_CYRILLIC
	 # define SUBLANG_SERBIAN_CYRILLIC 0x03
	 # endif
	-# ifndef SUBLANG_SINDHI_INDIA
	-# define SUBLANG_SINDHI_INDIA 0x00
	-# endif
	 # ifndef SUBLANG_SINDHI_PAKISTAN
	 # define SUBLANG_SINDHI_PAKISTAN 0x01
	 # endif
	+# ifndef SUBLANG_SINDHI_AFGHANISTAN
	+# define SUBLANG_SINDHI_AFGHANISTAN 0x02
	+# endif
	 # ifndef SUBLANG_SPANISH_GUATEMALA
	 # define SUBLANG_SPANISH_GUATEMALA 0x04
	 # endif
	@@ -670,14 +670,14 @@
	 # ifndef SUBLANG_TAMAZIGHT_ARABIC
	 # define SUBLANG_TAMAZIGHT_ARABIC 0x01
	 # endif
	-# ifndef SUBLANG_TAMAZIGHT_LATIN
	-# define SUBLANG_TAMAZIGHT_LATIN 0x02
	+# ifndef SUBLANG_TAMAZIGHT_ALGERIA_LATIN
	+# define SUBLANG_TAMAZIGHT_ALGERIA_LATIN 0x02
	 # endif
	 # ifndef SUBLANG_TIGRINYA_ETHIOPIA
	-# define SUBLANG_TIGRINYA_ETHIOPIA 0x00
	+# define SUBLANG_TIGRINYA_ETHIOPIA 0x01
	 # endif
	 # ifndef SUBLANG_TIGRINYA_ERITREA
	-# define SUBLANG_TIGRINYA_ERITREA 0x01
	+# define SUBLANG_TIGRINYA_ERITREA 0x02
	 # endif
	 # ifndef SUBLANG_URDU_PAKISTAN
	 # define SUBLANG_URDU_PAKISTAN 0x01
	@@ -1378,8 +1378,8 @@ _nl_locale_name_default (void)
	       case LANG_SINDHI:
	 	switch (sub)
	 	  {
	-	  case SUBLANG_SINDHI_INDIA: return "sd_IN";
	 	  case SUBLANG_SINDHI_PAKISTAN: return "sd_PK";
	+	  case SUBLANG_SINDHI_AFGHANISTAN: return "sd_AF";
	 	  }
	 	return "sd";
	       case LANG_SINHALESE: return "si_LK";
	@@ -1432,7 +1432,7 @@ _nl_locale_name_default (void)
	 	  {
	 	  /* FIXME: Adjust this when Tamazight locales appear on Unix.  */
	 	  case SUBLANG_TAMAZIGHT_ARABIC: return "ber_MA@arabic";
	-	  case SUBLANG_TAMAZIGHT_LATIN: return "ber_MA@latin";
	+	  case SUBLANG_TAMAZIGHT_ALGERIA_LATIN: return "ber_DZ@latin";
	 	  }
	 	return "ber_MA";
	       case LANG_TAMIL:
	END
# !!! Try upgrading to latest version - *this* one dir shouldn't drag in much
# (But disable threading then - for use with mtPaint, it's a complete waste)
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--without-libexpat-prefix
	make -C gettext-runtime/intl
	make -C gettext-runtime/intl install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
	EXPORT
}

DEP_glib="libiconv gettext"
BUILD_glib ()
{
	UNPACK "glib-2.6.*.tar.*" || return 0
	# Derived from gettext's git ec56ad8f2bf513a88efc3dba44953edff3909207
	# (Chosen instead of the route GLib 2.14+ has taken with svn 5257, to
	# preserve consistent behaviour across various Windows versions despite
	# their multiple bugs.)
	patch -p1 <<- 'END' # Is needed to fix build failure with w32api 3.15+
	diff -udpr glib-2.6.6_/glib/gwin32.c glib-2.6.6/glib/gwin32.c
	--- glib-2.6.6_/glib/gwin32.c	2005-03-14 07:02:07.000000000 +0300
	+++ glib-2.6.6/glib/gwin32.c	2011-01-14 00:52:49.000000000 +0300
	@@ -416,10 +416,10 @@ g_win32_ftruncate (gint  fd,
	 #define SUBLANG_AZERI_CYRILLIC 0x02
	 #endif
	 #ifndef SUBLANG_BENGALI_INDIA
	-#define SUBLANG_BENGALI_INDIA 0x00
	+#define SUBLANG_BENGALI_INDIA 0x01
	 #endif
	 #ifndef SUBLANG_BENGALI_BANGLADESH
	-#define SUBLANG_BENGALI_BANGLADESH 0x01
	+#define SUBLANG_BENGALI_BANGLADESH 0x02
	 #endif
	 #ifndef SUBLANG_CHINESE_MACAU
	 #define SUBLANG_CHINESE_MACAU 0x05
	@@ -512,16 +512,16 @@ g_win32_ftruncate (gint  fd,
	 #define SUBLANG_NEPALI_INDIA 0x02
	 #endif
	 #ifndef SUBLANG_PUNJABI_INDIA
	-#define SUBLANG_PUNJABI_INDIA 0x00
	+#define SUBLANG_PUNJABI_INDIA 0x01
	 #endif
	 #ifndef SUBLANG_PUNJABI_PAKISTAN
	-#define SUBLANG_PUNJABI_PAKISTAN 0x01
	+#define SUBLANG_PUNJABI_PAKISTAN 0x02
	 #endif
	 #ifndef SUBLANG_ROMANIAN_ROMANIA
	-#define SUBLANG_ROMANIAN_ROMANIA 0x00
	+#define SUBLANG_ROMANIAN_ROMANIA 0x01
	 #endif
	 #ifndef SUBLANG_ROMANIAN_MOLDOVA
	-#define SUBLANG_ROMANIAN_MOLDOVA 0x01
	+#define SUBLANG_ROMANIAN_MOLDOVA 0x02
	 #endif
	 #ifndef SUBLANG_SERBIAN_LATIN
	 #define SUBLANG_SERBIAN_LATIN 0x02
	@@ -529,12 +529,12 @@ g_win32_ftruncate (gint  fd,
	 #ifndef SUBLANG_SERBIAN_CYRILLIC
	 #define SUBLANG_SERBIAN_CYRILLIC 0x03
	 #endif
	-#ifndef SUBLANG_SINDHI_INDIA
	-#define SUBLANG_SINDHI_INDIA 0x00
	-#endif
	 #ifndef SUBLANG_SINDHI_PAKISTAN
	 #define SUBLANG_SINDHI_PAKISTAN 0x01
	 #endif
	+#ifndef SUBLANG_SINDHI_AFGHANISTAN
	+#define SUBLANG_SINDHI_AFGHANISTAN 0x02
	+#endif
	 #ifndef SUBLANG_SPANISH_GUATEMALA
	 #define SUBLANG_SPANISH_GUATEMALA 0x04
	 #endif
	@@ -592,14 +592,14 @@ g_win32_ftruncate (gint  fd,
	 #ifndef SUBLANG_TAMAZIGHT_ARABIC
	 #define SUBLANG_TAMAZIGHT_ARABIC 0x01
	 #endif
	-#ifndef SUBLANG_TAMAZIGHT_LATIN
	-#define SUBLANG_TAMAZIGHT_LATIN 0x02
	+#ifndef SUBLANG_TAMAZIGHT_ALGERIA_LATIN
	+#define SUBLANG_TAMAZIGHT_ALGERIA_LATIN 0x02
	 #endif
	 #ifndef SUBLANG_TIGRINYA_ETHIOPIA
	-#define SUBLANG_TIGRINYA_ETHIOPIA 0x00
	+#define SUBLANG_TIGRINYA_ETHIOPIA 0x01
	 #endif
	 #ifndef SUBLANG_TIGRINYA_ERITREA
	-#define SUBLANG_TIGRINYA_ERITREA 0x01
	+#define SUBLANG_TIGRINYA_ERITREA 0x02
	 #endif
	 #ifndef SUBLANG_URDU_PAKISTAN
	 #define SUBLANG_URDU_PAKISTAN 0x01
	@@ -922,8 +922,8 @@ g_win32_getlocale (void)
	     case LANG_SINDHI: l = "sd";
	       switch (sub)
	 	{
	-	case SUBLANG_SINDHI_INDIA: sl = "IN"; break;
	 	case SUBLANG_SINDHI_PAKISTAN: sl = "PK"; break;
	+	case SUBLANG_SINDHI_AFGHANISTAN: sl = "AF"; break;
	 	}
	       break;
	     case LANG_SINHALESE: l = "si"; sl = "LK"; break;
	END
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/glib-2.0" "$TOPDIR/lib/glib-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/glib*.pc "$TOPDIR/lib/pkgconfig"/gmodule*.pc
	rm -f "$TOPDIR/lib/pkgconfig"/gobject*.pc "$TOPDIR/lib/pkgconfig"/gthread*.pc
	rm -f "$TOPDIR/lib"/libglib*.dll.a "$TOPDIR/lib"/libgmodule*.dll.a
	rm -f "$TOPDIR/lib"/libgobject*.dll.a "$TOPDIR/lib"/libgthread*.dll.a
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib -mno-cygwin" \
	CFLAGS="-mno-cygwin -mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no \
		--with-threads=win32 glib_cv_stack_grows=no
# !!! See about "-fno-strict-aliasing": is it needed here with GCC 4, or not?
	make
	make install-strip DESTDIR="$DESTDIR"
	find "$DEST/" -name '*.dll' -exec "$TARGET_STRIP" --strip-unneeded {} +
	mv "$DEST/share/locale" "$DEST/lib/locale"
	echo 'PKG="$PKG bin/gspawn-win32-helper.exe"' > "$DESTDIR.install"
	echo 'DEV="$DEV lib/glib-2.0/"' >> "$DESTDIR.install"
	EXPORT
}

DEP_atk="glib gettext"
BUILD_atk ()
{
	UNPACK "atk-1.9.*.tar.*" || return 0
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/atk-1.0"
	rm -f "$TOPDIR/lib/pkgconfig"/atk.pc "$TOPDIR/lib"/libatk*.dll.a
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib -mno-cygwin" \
	CFLAGS="-mno-cygwin -mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no
	make
	make install-strip DESTDIR="$DESTDIR"
	find "$DEST/" -name '*.dll' -exec "$TARGET_STRIP" --strip-unneeded {} +
	mv "$DEST/share/locale" "$DEST/lib/locale"
	EXPORT
}

DEP_pango="glib"
BUILD_pango ()
{
	UNPACK "pango-1.8.*.tar.*" || return 0
	patch -p1 -i "$SRCDIR/pango182_1wj.patch"
	# !!! Remove traces of existing install - else confusion will result
	rm -rf "$TOPDIR/include/pango-1.0"
	rm -f "$TOPDIR/lib/pkgconfig"/pango*.pc "$TOPDIR/lib"/libpango*.dll.a
	# !!! The only way to disable Fc backend is to report Fontconfig missing
	rm -f "$TOPDIR/lib/pkgconfig/fontconfig.pc"
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib -mno-cygwin" \
	CFLAGS="-mno-cygwin -mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no \
		--without-x --with-usp10=$TOPDIR/include
	make
	make install-strip DESTDIR="$DESTDIR"
	find "$DEST/" -name '*.dll' -exec "$TARGET_STRIP" --strip-unneeded {} +
	find "$DEST/lib/pango/1.4.0/" \( -name '*.dll.a' -o -name '*.la' \) -delete
	mkdir -p "$DEST/etc/pango"
	cp -p "$SRCDIR/pango.aliases" "$DEST/etc/pango" || true
	cp -p "$SRCDIR/pango.modules" "$DEST/etc/pango" || true
	echo 'PKG="$PKG lib/pango/"' > "$DESTDIR.install"
	EXPORT
}

DEP_gtk="glib gettext libtiff libpng libjpeg atk pango"
BUILD_gtk ()
{
	UNPACK "wtkit*.zip" -LL && COPY_BINARIES
	touch "$DEST.exclude" # Do not package the contents
	local WTKIT
	WTKIT="$DEST"
	UNPACK "gtk+-2.6.7.tar.*" || return 0
	patch -p1 -i "$SRCDIR/gtk267_5wj.patch"
	# !!! Remove traces of existing install - else confusion will result
	rm -rf "$TOPDIR/include/gtk-2.0" "$TOPDIR/lib/gtk-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/g[dt]k*.pc "$TOPDIR/lib"/libg[dt]k*.dll.a
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib -mno-cygwin" \
	CFLAGS="-mno-cygwin -mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no \
		--with-gdktarget=win32 --with-wintab="$WTKIT"
	make
	make install-strip DESTDIR="$DESTDIR"
	find "$DEST/" -name '*.dll' -exec "$TARGET_STRIP" --strip-unneeded {} +
	find "$DEST/lib/gtk-2.0/2.4.0/" \( -name '*.dll.a' -o -name '*.la' \) -delete
	mv "$DEST/share/locale" "$DEST/lib/locale"
	rm -f "$DEST/lib/locale"/*/LC_MESSAGES/gtk20-properties.mo
	mkdir -p "$DEST/etc/gtk-2.0"
	cp -p "$SRCDIR/gdk-pixbuf.loaders" "$DEST/etc/gtk-2.0" || true
	cp -p "$SRCDIR/gtkrc" "$DEST/etc/gtk-2.0" || true
	printf '%s%s\n' 'PKG="$PKG lib/gtk-2.0/2.4.0/engines/libwimp.dll ' \
		'lib/gtk-2.0/2.4.0/loaders/"' > "$DESTDIR.install"
	echo 'DEV="$DEV lib/gtk-2.0/include/"' >> "$DESTDIR.install"
	EXPORT
}

BUILD_gifsicle ()
{
	UNPACK "gifsicle-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--without-x
	make
	make install-strip DESTDIR="$DESTDIR"
	echo 'PKG="$PKG bin/gifsicle.exe"' > "$DESTDIR.install"
	echo 'DEV=' >> "$DESTDIR.install"
}

DEP_mtpaint="$LIBS gifsicle"
BUILD_mtpaint ()
{
	UNPACK "mtpaint-*.tar.bz2" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		release intl
	make
	make install DESTDIR="$DESTDIR"
	echo 'PKG="$PKG bin/"' > "$DESTDIR.install"
	echo 'DEV=' >> "$DESTDIR.install"
}

# Collect compiled files and drop them into runtime and development packages
INST_all ()
{
	rm -rf "$PKGDIR"/* "$DEVDIR"/*
	local ZAD
	local PERED
	local FILE
	local DIR
	local DEST
	local PKG
	local DEV

	# Support locales only of GLib-based libs
	local LOCALES_SV
	LOCALES_SV="$LOCALES"
	LOCALES=""
	for ZAD in $LOCALES_SV
	do
		LOCALES="$LOCALES lib/locale/$ZAD/"
	done
	LOCALES="${LOCALES# }"

	for ZAD in "$INSDIR"/*
	do
		[ ! -d "$ZAD" -o -e "$ZAD.exclude" ] && continue
		echo "$ZAD" # Progress indicator
		PKG="bin/*.dll etc/ $LOCALES"
		DEV="include/ lib/*.a lib/pkgconfig/"
		if [ -f "$ZAD.install" ]
		then
# !!! REMEMBER TO CREATE THESE FOR PACKAGES WITH NONDEFAULT CONTENT
			. "$ZAD.install"
		fi
		DIR="$PKGDIR"
		while [ -n "$PKG" -o -n "$DEV" ]
		do
			PKG="$PKG "
			while [ -n "$PKG" ]
			do
				PERED="${PKG%% *}"
				PKG="${PKG#$PERED }"
				[ -z "$PERED" ] && continue
				for FILE in "$ZAD$WPREFIX"/$PERED
				do
					[ -e "$FILE" ] || continue
					DEST="$DIR/${FILE#$ZAD}"
					mkdir -p "${DEST%/?*}"
					cp -fpRT "$FILE" "$DEST"
				done
			done
			DIR="$DEVDIR"
			PKG="$DEV"
			DEV=
		done
	done
	LOCALES="$LOCALES_SV"

	cd "$PKGDIR/bin"
	if [ ! -f "$PKGDIR/etc/pango/pango.modules" ]
	then
		mkdir -p "$PKGDIR/etc/pango"
		wine "$INSDIR"/pango-*"$WPREFIX"/bin/pango-querymodules.exe | \
			sed -e 's|\\\\\?|/|g' \
			-e "s|.:.\+\($WPREFIX/lib/pango\)|\1|" > \
			"$PKGDIR/etc/pango/pango.modules"
	fi
	if [ ! -f "$PKGDIR/etc/gtk-2.0/gdk-pixbuf.loaders" ]
	then
		mkdir -p "$PKGDIR/etc/gtk-2.0"
		wine "$INSDIR"/gtk+-*"$WPREFIX"/bin/gdk-pixbuf-query-loaders.exe | \
			sed -e '/:/ s|\\\\\?|/|g' \
			-e "s|.:.\+\($WPREFIX/lib/gtk-2\)|\1|" > \
			"$PKGDIR/etc/gtk-2.0/gdk-pixbuf.loaders"
	fi
}

DEP_libs="$LIBS"
BUILD_libs ()
{
	INST_all
}

DEP_all="$LIBS $PROGRAMS"
BUILD_all ()
{
	INST_all
}

# Ctrl+C terminates script
trap exit INT

# Parse commandline
if [ "$1" = new ]
then
	REBUILD=1
	shift
fi

ZAD=
for ZAD in "$@"
do
	case "$ZAD" in
	only-?* | no-?* ) DEP="${ZAD#*-}";;
	* ) DEP="$ZAD";;
	esac
	if [ "${COMPONENTS#* $DEP }" = "$COMPONENTS" ]
	then
		echo "Unknown parameter: '$ZAD'"
		continue
	fi
	if [ "${ZAD%$DEP}" = "no-" ]
	then # Without component
		eval "HAVE_$DEP=1"
	elif [ "${ZAD%$DEP}" = "only-" ]
	then # Without dependencies
		eval "NEED_$DEP=2" # Force compile
		eval "DEPS=\$DEP_$DEP"
		for DEP in $DEPS
		do
			eval "HAVE_$DEP=1"
		done
	else # With component
		eval "NEED_$DEP=2"
	fi
done
if [ -z "$ZAD" ]
then # Build "all" by default
	NEED_all=2
fi

set -e # "set -eu" feels like overkill

# Prepare build directories
mkdir -p "$WRKDIR" "$INSDIR" "$PKGDIR" "$DEVDIR"
test -d "$TOPDIR/include" || cp -sR "$MPREFIX/include/" "$TOPDIR/include/"
test -d "$TOPDIR/lib" || cp -sR "$MPREFIX/lib/" "$TOPDIR/lib/"

# Create links for what misconfigured cross-compilers fail to provide
mkdir -p "$TOPDIR/bin"
LONGCC=`PATH="$LONGPATH" which $MTARGET-gcc`
for BINARY in ${LONGCC%/*}/$MTARGET-*
do
	ln -sf "$BINARY" "$TOPDIR/bin/${BINARY##*/$MTARGET-}"
done

# Prepare fake pkg-config
OLD_PKGCONFIG=`which pkg-config`
PKG_CONFIG="$TOPDIR/pkg-config"
export PKG_CONFIG
rm -f "$PKG_CONFIG"
cat << PKGCONFIG > "$PKG_CONFIG"
#!/bin/sh

export PKG_CONFIG_LIBDIR="$TOPDIR/lib/pkgconfig"
export PKG_CONFIG_PATH=

# pkg-config doesn't like --define-variable with these
if [ "x\${*#*--atleast-pkgconfig-version}" != "x\${*#*--atleast-version}" ]
then
	exec "$OLD_PKGCONFIG" "\$@"
else
	exec "$OLD_PKGCONFIG" --define-variable=prefix="$TOPDIR" "\$@"
fi
PKGCONFIG
chmod +x "$PKG_CONFIG"
# Not actually needed for the packages in here, but why not
ln -sf pkg-config "$TOPDIR/$MTARGET-pkg-config"

# Provide (stripped-down) GLib 2.6.6 scripts for ATK & Pango
SCRIPTDIR="$WRKDIR/glib/build/win32"
mkdir -p "$SCRIPTDIR"
cat << 'END' > "$SCRIPTDIR/compile-resource"
#!/bin/sh
rcfile=$1
resfile=$2
if [ -f $rcfile ]; then
    basename=`basename $rcfile .rc`
    if [ -f $basename-build.stamp ]; then
	read number <$basename-build.stamp
	buildnumber=`expr $number + 1`
	echo Build number $buildnumber
	echo $buildnumber >$basename-build.stamp
    else
	echo Using zero as build number
        buildnumber=0
    fi
    m4 -DBUILDNUMBER=$buildnumber <$rcfile >$$.rc &&
	${WINDRES-windres} $$.rc $resfile &&
	rm $$.rc
else
    exit 1
fi
END
cat << 'END' > "$SCRIPTDIR/lt-compile-resource"
#!/bin/sh
rcfile=$1
lo=$2
case "$lo" in
*.lo) 
    resfile=.libs/`basename $lo .lo`.o
    ;;
*)
    echo libtool object name should end with .lo
    exit 1
    ;;
esac
d=`dirname $0`
[ ! -d .libs ] && mkdir .libs
o_files_in_dotlibs=`echo .libs/*.o`
case "$o_files_in_dotlibs" in
    .libs/\*.o)
	use_script=false
	;;
    *)  use_script=true
	;;
esac
o_files_in_dot=$(echo ./*.o)
case "$o_files_in_dot" in
    ./\*.o)
    	;;
    *)	use_script=true
    	;;
esac    	
$d/compile-resource $rcfile $resfile && {
    if [ $use_script = true ]; then
	(echo "# $lo"
	echo "# Generated by lt-compile-resource, compatible with libtool"
	echo "pic_object=$resfile"
	echo "non_pic_object=none") >$lo
    else
	mv $resfile $lo
    fi
    exit 0
}
exit 1
END
chmod +x "$SCRIPTDIR"/*

# A little extra safety
LIBS= PROGRAMS= PHONY=

# Do the build in proper order
READY=
while [ "$READY" != 2 ]
do
	READY=2
	for ZAD in $COMPONENTS
	do
		if ((NEED_$ZAD<=HAVE_$ZAD))
		then
			continue
		fi
		READY=1
		eval "DEPS=\$DEP_$ZAD"
		for DEP in $DEPS
		do
			eval "NEED_$DEP=\${NEED_$DEP:-1}"
			if ((NEED_$DEP>HAVE_$DEP))
			then
#				echo "$ZAD needs $DEP"
				READY=
			fi
		done
		if [ $READY ]
		then
			echo "Build $ZAD"
			eval "BUILD_$ZAD"
			eval "HAVE_$ZAD=\$NEED_$ZAD"
		fi
	done
done
