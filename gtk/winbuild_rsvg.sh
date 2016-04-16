#!/bin/sh
# winbuild_rsvg.sh - cross-compile static rsvg-convert.exe for Windows

# Copyright (C) 2016 Dmitry Groshev

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

LIBS="dev glib pixman cairo pango libxml2 libcroco librsvg gdkpixbuf"
PHONY="all"

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
	*.tar.xz)  FDIR=.tar.xz ; FCMD="tar $2 --use-compress-program xz -xf" ;;
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

BUILD_dev ()
{
	UNPACK "dev.tar.*" || return 0
	touch "$DEST.exclude" # Do not package the contents
	ln -sf "$FDIR" "$DEST"
	EXPORT
}

DEP_glib="dev" # libiconv gettext
BUILD_glib ()
{
	UNPACK "glib-*.tar.*" || return 0
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/glib-2.0" "$TOPDIR/lib/glib-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/glib*.pc "$TOPDIR/lib/pkgconfig"/gmodule*.pc
	rm -f "$TOPDIR/lib/pkgconfig"/gobject*.pc "$TOPDIR/lib/pkgconfig"/gthread*.pc
	rm -f "$TOPDIR/lib"/libglib*.dll.a "$TOPDIR/lib"/libgmodule*.dll.a
	rm -f "$TOPDIR/lib"/libgobject*.dll.a "$TOPDIR/lib"/libgthread*.dll.a
	PATH="$ALLPATH"
	./configure --disable-gtk-doc-html --with-threads=win32 \
		--disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
# !!! Try reducing the featureset some more
	make
	make install DESTDIR="$DESTDIR"
	# Fix libs
	sed -i 's/^Libs:.*/& -lole32 -lws2_32/' "$DEST/lib/pkgconfig/glib-2.0.pc"
	sed -i 's/^Libs:.*/& -lshlwapi -ldnsapi/' "$DEST/lib/pkgconfig/gio-2.0.pc"
	EXPORT
	ln -sf "$DEST/bin/glib-mkenums" "$TOPDIR/glib-mkenums"
}

DEP_gdkpixbuf="glib dev" # gettext libtiff libpng libjpeg jasper
BUILD_gdkpixbuf ()
{
	UNPACK "gdk-pixbuf-*.tar.*" || return 0
	# Remove traces of existing install - *.dll.a can break static build
	rm -rf "$TOPDIR/include/gtk-2.0/gdk-pixbuf" "$TOPDIR/lib/pango"
	rm -f "$TOPDIR/lib/pkgconfig"/gdk-pixbuf*.pc
	rm -f "$TOPDIR/lib"/libgdk_pixbuf*.dll.a
	patch <<- 'END'
	--- configure_	2010-11-06 02:56:08.000000000 +0300
	+++ configure	2016-04-10 10:42:55.000000000 +0300
	@@ -4650,19 +4650,6 @@
	     ;;
	 esac
	 
	-if test "$os_win32" = "yes"; then
	-  if test x$enable_static = xyes -o x$enable_static = x; then
	-    { $as_echo "$as_me:${as_lineno-$LINENO}: WARNING: Disabling static library build, must build as DLL on Windows." >&5
	-$as_echo "$as_me: WARNING: Disabling static library build, must build as DLL on Windows." >&2;}
	-    enable_static=no
	-  fi
	-  if test x$enable_shared = xno; then
	-    { $as_echo "$as_me:${as_lineno-$LINENO}: WARNING: Enabling shared library build, must build as DLL on Windows." >&5
	-$as_echo "$as_me: WARNING: Enabling shared library build, must build as DLL on Windows." >&2;}
	-  fi
	-  enable_shared=yes
	-fi
	-
	 
	 case `pwd` in
	   *\ * | *\	*)
	END
	WANT_JASPER=
	[ "`echo \"$TOPDIR\"/lib/libjasper*`" != "$TOPDIR/lib/libjasper*" ] && \
		WANT_JASPER=--with-libjasper
	PATH="$ALLPATH"
	./configure --disable-modules --with-included-loaders $WANT_JASPER \
		--disable-gdiplus --disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
#	LIBS="`'$(TARGET)-pkg-config' --libs libtiff-4`"
	make
	make install DESTDIR="$DESTDIR"
	EXPORT
}

DEP_pixman="glib"
BUILD_pixman ()
{
	UNPACK "pixman-*.tar.*" || return 0
	PATH="$LONGPATH"
	./configure --disable-sse2 --disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
	make
	make install DESTDIR="$DESTDIR"
	EXPORT
}

# Depends on libpng too, but will match version later - use default one for now
DEP_cairo="glib pixman"
BUILD_cairo ()
{
	UNPACK "cairo-*.tar.*" || return 0
	sed -i 's,^\(Libs:.*\),\1 @CAIRO_NONPKGCONFIG_LIBS@,' src/cairo.pc.in
	PATH="$ALLPATH"
	./configure --disable-xlib --disable-xlib-xrender --disable-ft \
		--disable-atomic --disable-pthread --disable-ps --disable-pdf \
		--disable-gtk-doc --disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET \
		CFLAGS="$CFLAGS -DCAIRO_WIN32_STATIC_BUILD" \
		LIBS="-lmsimg32 -lgdi32 `pkg-config pixman-1 --libs`"
# Is reasonable to try disabling SVG backend too
	make
	make install DESTDIR="$DESTDIR"
	EXPORT
}

DEP_pango="glib cairo"
BUILD_pango ()
{
	UNPACK "pango-*.tar.*" || return 0
	# Remove traces of existing install - *.dll.a break rsvg-convert link
	rm -rf "$TOPDIR/include/pango-1.0" "$TOPDIR/lib/pango"
	rm -f "$TOPDIR/lib/pkgconfig"/pango*.pc
	rm -f "$TOPDIR/lib"/libpango*.dll.a
	patch -p1 -l -i "$SRCDIR/pango182_1wj.patch"
	patch <<- 'END'
	--- configure0	2007-07-28 01:27:07.000000000 +0400
	+++ configure	2016-04-10 04:50:38.000000000 +0300
	@@ -4495,19 +4495,6 @@
	 
	 
	 
	-if test "$pango_os_win32" = "yes"; then
	-  if test x$enable_static = xyes -o x$enable_static = x; then
	-    { echo "$as_me:$LINENO: WARNING: Disabling static library build, must build as DLL on Windows." >&5
	-echo "$as_me: WARNING: Disabling static library build, must build as DLL on Windows." >&2;}
	-    enable_static=no
	-  fi
	-  if test x$enable_shared = xno; then
	-    { echo "$as_me:$LINENO: WARNING: Enabling shared library build, must build as DLL on Windows." >&5
	-echo "$as_me: WARNING: Enabling shared library build, must build as DLL on Windows." >&2;}
	-  fi
	-  enable_shared=yes
	-fi
	-
	 # Check whether --enable-shared was given.
	 if test "${enable_shared+set}" = set; then
	   enableval=$enable_shared; p=${PACKAGE-default}
	END
	PATH="$ALLPATH"
	./configure --enable-explicit-deps --with-included-modules \
		--without-dynamic-modules --enable-shared=no --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET \
		LIBS="`pkg-config cairo --libs` -lpng12" #-lws2_32
# libpng's name must match the one Cairo uses (and *can* use!)
	make
	make install DESTDIR="$DESTDIR"
	# Fix libs
	sed -i 's/^Requires:.*/& pangowin32/' "$DEST/lib/pkgconfig/pangocairo.pc"
	EXPORT
}

DEP_libxml2="dev" # zlib libiconv
BUILD_libxml2 ()
{
	UNPACK "libxml2-*.tar.*" || return 0
	PATH="$ALLPATH"
	./configure --without-debug --without-python --without-threads \
		--disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
	make
	make install DESTDIR="$DESTDIR"
	# Fix libs
	sed -i 's/^Libs:.*/& -lws2_32 -lz -liconv/' "$DEST/lib/pkgconfig/libxml-2.0.pc"
	EXPORT
}

DEP_libcroco="glib libxml2"
BUILD_libcroco ()
{
	UNPACK "libcroco-*.tar.*" || return 0
	PATH="$ALLPATH"
	./configure --disable-gtk-doc --disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
	make
	make install DESTDIR="$DESTDIR"
	EXPORT
}

DEP_librsvg="glib libxml2 cairo libcroco gdkpixbuf"
BUILD_librsvg ()
{
	UNPACK "librsvg-*.tar.*" || return 0
	patch <<- 'END'
	--- rsvg-gobject.c0	2012-08-19 21:59:52.000000000 +0400
	+++ rsvg-gobject.c	2016-04-10 04:20:18.000000000 +0300
	@@ -41,6 +41,23 @@
	 #include "rsvg-defs.h"
	 #include "rsvg.h"
	 
	+void
	+g_clear_object (volatile GObject **object_ptr)
	+{
	+  gpointer *ptr = (gpointer) object_ptr;
	+  gpointer old;
	+
	+  /* This is a little frustrating.
	+   * Would be nice to have an atomic exchange (with no compare).
	+   */
	+  do
	+    old = g_atomic_pointer_get (ptr);
	+  while G_UNLIKELY (!g_atomic_pointer_compare_and_exchange (ptr, old, NULL));
	+
	+  if (old)
	+    g_object_unref (old);
	+}
	+
	 enum {
	     PROP_0,
	     PROP_FLAGS,
	END
	PATH="$ALLPATH"
	./configure --enable-introspection=no --disable-pixbuf-loader \
		--disable-gtk-doc --disable-shared --enable-static \
		--prefix="$WPREFIX" --host=$MTARGET
	make
	make install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.exe
	echo 'PKG="$PKG bin/rsvg-convert.exe"' > "$DESTDIR.install"
	EXPORT
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

	for ZAD in "$INSDIR"/*
	do
		[ ! -d "$ZAD" -o -e "$ZAD.exclude" ] && continue
		echo "$ZAD" # Progress indicator
		PKG="bin/*.dll"
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
}

DEP_all="$LIBS"
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

# Give GLib 2.25+ a fake binary for its fake dependency
if ! ( cd "$TOPDIR" && which glib-compile-schemas )
then
	ln -sf `which true` "$TOPDIR/glib-compile-schemas"
fi

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
