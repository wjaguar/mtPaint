#!/bin/sh
# winbuild64.sh - cross-compile GTK+ and its dependencies for Windows 64-bit

# Copyright (C) 2010,2011,2017,2019,2020 Dmitry Groshev

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

LIBS="libgcc pthread libcxx giflib zlib xz zstd libjpeg libwebp libpng "\
"libtiff openjpeg lcms bzip2 freetype_base libiconv gettext expat fontconfig "\
"pcre libffi glib pixman cairo icu harfbuzz freetype fribidi pango gdkpixbuf "\
"libxml2 libcroco libgsf librsvg atk gtk"
PROGRAMS="gifsicle mtpaint mtpaint_handbook"
PHONY="libs all"
TOOLS="dev vars"
LOCALES="es cs fr pt pt_BR de pl tr zh_TW sk zh_CN ja ru gl nl it sv tl hu"
# Applied only to GTK+ ATM
#OPTIMIZE="-march=i686 -O2 -fweb -fomit-frame-pointer -fmodulo-sched -Wno-pointer-sign"

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
#MPREFIX=/usr/i586-mingw32-4.2.4
#MTARGET=i586-mingw32

# mingw-cross-env
# http://mingw-cross-env.nongnu.org/
#MPREFIX=~/mingw-cross-env-2.18/usr
#MTARGET=i686-pc-mingw32

# MXE
# http://mxe.cc/
#MPREFIX=~/strawberry-mxe-master/usr
#MPREFIX=~/mxe/usr
#MTARGET=x86_64-w64-mingw32.shared

# MinGW-w64, SpeedBlue-like build
MPREFIX=/usr/x86_64-w64-mingw32-5.5.0
MTARGET=x86_64-w64-mingw32

ALLPATHS='
DEFPATH="$PATH"
LONGPATH="$TOPDIR:$MPREFIX/bin:$PATH"
SHORTPATH="$TOPDIR:$TOPDIR/bin:$PATH"
ALLPATH="$TOPDIR:$TOPDIR/bin:$MPREFIX/bin:$PATH"
'
eval "$ALLPATHS"

########################

# Initialize vars
COMPONENTS=" $LIBS $PROGRAMS $PHONY $TOOLS "
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
	*.tar.bz2) FDIR=.tar.bz2 ; FCMD="tar -xf" ;;
	*.tar.gz)  FDIR=.tar.gz ; FCMD="tar -xf" ;;
	*.tar.xz)  FDIR=.tar.xz ; FCMD="tar --use-compress-program xz -xf" ;;
	*.tar.zst) FDIR=.tar.zst ; FCMD="tar --use-compress-program zstd -xf" ;;
	*.zip)     FDIR=.zip ; FCMD="unzip" ;;
	*)         echo "ERROR: $FNAME unknown archive type" ; exit 1 ;;
	esac
	FDIR="$WRKDIR/${FNAME%$FDIR}$2"
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

# Relativize .pc files
RELATIVIZE ()
{
	local ZAD
	for ZAD in "$@"
	do
		grep 'exec_prefix=\$' "$ZAD" && continue
		# Relativize pkgconfig file
		cat <<- PKGFIX > "$ZAD"_
		prefix=$WPREFIX
		exec_prefix=\${prefix}
		libdir=\${exec_prefix}/lib
		includedir=\${prefix}/include
		
		`sed -e '/^Name:/,$!d' "$ZAD"`
		PKGFIX
		mv -f "$ZAD"_ "$ZAD"
	done
}

# Prepare one of GCC's DLLs for packaging
IMPORT_DLL ()
{
	DESTDIR="$INSDIR/$1"
	DEST="$DESTDIR$WPREFIX"
	[ -d "$DESTDIR" -a ! "$REBUILD" ] && return 0
	rm -rf "$DESTDIR"
	mkdir -p "$DEST/bin"

	# Check if it can be linked dynamically
	local LIB
	LIB=${2:-lib$1.dll.a}
	LIB=`"$TOPDIR/bin/gcc" --print-file-name=$LIB`
	[ -f "$LIB" ] || return 0

	# Search for DLL in corresponding libdir, then in bindir under prefix
	local DLL
	DLL=`"$TOPDIR/bin/dlltool" --identify "$LIB"`
	LIB="${LIB%/*}/$DLL"
	[ -f "$LIB" ] || LIB="$MPREFIX/$MTARGET/bin/$DLL"
	[ -f "$LIB" ] || LIB="$MPREFIX/bin/$DLL"
	if [ ! -f "$LIB" ]
	then
		echo "ERROR: $1 DLL '$DLL' not found"
		exit 1
	fi

	cp -fp "$LIB" "$DEST/bin"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
}

# Tools
TARGET_STRIP="$MPREFIX/bin/$MTARGET-strip"

BUILD_pthread ()
{
	# Check if gcc needs the library at all
#	cd "$WRKDIR"
#	echo "main() { ; }" > _conf.c
#	"$TOPDIR/bin/gcc" -pthread _conf.c -o _conf.tmp > /dev/null 2>&1 || return 0

	IMPORT_DLL pthread
}

DEP_libgcc="pthread"
BUILD_libgcc ()
{
	IMPORT_DLL libgcc libgcc_s.a
}

DEP_libcxx="libgcc pthread"
BUILD_libcxx ()
{
	IMPORT_DLL stdc++
}

BUILD_giflib ()
{
	UNPACK "giflib-*.tar.*" || return 0
	patch -p0 <<- 'MINGW' # Fix makefile for MinGW
	--- Makefile.0	2019-06-24 19:08:57.000000000 +0300
	+++ Makefile	2020-11-06 01:27:06.702201012 +0200
	@@ -29,11 +29,11 @@
	 LIBVER=$(LIBMAJOR).$(LIBMINOR).$(LIBPOINT)
	 
	 SOURCES = dgif_lib.c egif_lib.c gifalloc.c gif_err.c gif_font.c \
	-	gif_hash.c openbsd-reallocarray.c
	+	gif_hash.c openbsd-reallocarray.c quantize.c
	 HEADERS = gif_hash.h  gif_lib.h  gif_lib_private.h
	 OBJECTS = $(SOURCES:.c=.o)
	 
	-USOURCES = qprintf.c quantize.c getarg.c 
	+USOURCES = qprintf.c getarg.c
	 UHEADERS = getarg.h
	 UOBJECTS = $(USOURCES:.c=.o)
	 
	@@ -61,27 +61,25 @@
	 
	 LDLIBS=libgif.a -lm
	 
	-all: libgif.so libgif.a libutil.so libutil.a $(UTILS)
	+all: libgif-$(LIBMAJOR).dll libgif.a libutil-$(LIBMAJOR).dll libutil.a $(UTILS)
	 	$(MAKE) -C doc
	 
	 $(UTILS):: libgif.a libutil.a
	 
	-libgif.so: $(OBJECTS) $(HEADERS)
	-	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,-soname -Wl,libgif.so.$(LIBMAJOR) -o libgif.so $(OBJECTS)
	+libgif-$(LIBMAJOR).dll: $(OBJECTS) $(HEADERS)
	+	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,--out-implib,libgif.dll.a -o libgif-$(LIBMAJOR).dll $(OBJECTS)
	 
	 libgif.a: $(OBJECTS) $(HEADERS)
	 	$(AR) rcs libgif.a $(OBJECTS)
	 
	-libutil.so: $(UOBJECTS) $(UHEADERS)
	-	$(CC) $(CFLAGS) -shared $(LDFLAGS) -Wl,-soname -Wl,libutil.so.$(LIBMAJOR) -o libutil.so $(UOBJECTS)
	+libutil-$(LIBMAJOR).dll: $(UOBJECTS) $(UHEADERS)
	+	$(CC) $(CFLAGS) -shared $(LDFLAGS)  -Wl,--out-implib,libutil.dll.a -o libutil-$(LIBMAJOR).dll $(UOBJECTS) -L. -lgif
	 
	 libutil.a: $(UOBJECTS) $(UHEADERS)
	 	$(AR) rcs libutil.a $(UOBJECTS)
	 
	 clean:
	-	rm -f $(UTILS) $(TARGET) libgetarg.a libgif.a libgif.so libutil.a libutil.so *.o
	-	rm -f libgif.so.$(LIBMAJOR).$(LIBMINOR).$(LIBPOINT)
	-	rm -f libgif.so.$(LIBMAJOR)
	+	rm -f $(UTILS) $(TARGET) libgetarg.a libgif.a libgif-$(LIBMAJOR).dll libgif.dll.a libutil.a libutil-$(LIBMAJOR).dll libutil.dll.a *.o
	 	rm -fr doc/*.1 *.html doc/staging
	 
	 check: all
	@@ -99,9 +97,8 @@
	 install-lib:
	 	$(INSTALL) -d "$(DESTDIR)$(LIBDIR)"
	 	$(INSTALL) -m 644 libgif.a "$(DESTDIR)$(LIBDIR)/libgif.a"
	-	$(INSTALL) -m 755 libgif.so "$(DESTDIR)$(LIBDIR)/libgif.so.$(LIBVER)"
	-	ln -sf libgif.so.$(LIBVER) "$(DESTDIR)$(LIBDIR)/libgif.so.$(LIBMAJOR)"
	-	ln -sf libgif.so.$(LIBMAJOR) "$(DESTDIR)$(LIBDIR)/libgif.so"
	+	$(INSTALL) -m 644 libgif.dll.a "$(DESTDIR)$(LIBDIR)/libgif.dll.a"
	+	$(INSTALL) -m 755 libgif-$(LIBMAJOR).dll "$(DESTDIR)$(BINDIR)/libgif-$(LIBMAJOR).dll"
	 install-man:
	 	$(INSTALL) -d "$(DESTDIR)$(MANDIR)/man1"
	 	$(INSTALL) -m 644 doc/*.1 "$(DESTDIR)$(MANDIR)/man1"
	@@ -112,7 +109,7 @@
	 	rm -f "$(DESTDIR)$(INCDIR)/gif_lib.h"
	 uninstall-lib:
	 	cd "$(DESTDIR)$(LIBDIR)" && \
	-		rm -f libgif.a libgif.so libgif.so.$(LIBMAJOR) libgif.so.$(LIBVER)
	+		rm -f libgif.a libgif-$(LIBMAJOR).dll libgif.dll.a
	 uninstall-man:
	 	cd "$(DESTDIR)$(MANDIR)/man1" && rm -f $(shell cd doc >/dev/null && echo *.1)
	 
	MINGW
	PATH="$SHORTPATH"
	make LDFLAGS=-static-libgcc
	local ZAD
	local INSTALLABLE
	INSTALLABLE="gif2rgb.exe gifbuild.exe giffix.exe giftext.exe giftool.exe gifclrmp.exe"
	for ZAD in $INSTALLABLE
	do
		[ -f $ZAD ] || cp ${ZAD%.exe} $ZAD
	done
	make install DESTDIR="$DESTDIR" PREFIX="$WPREFIX" LDFLAGS=-static-libgcc \
		INSTALLABLE="$INSTALLABLE"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*
	EXPORT
}

BUILD_zlib ()
{
	UNPACK "zlib-*.tar.*" || return 0
	PATH="$SHORTPATH"
	./configure --prefix="$WPREFIX" -shared -static
	make -f win32/Makefile.gcc install SHARED_MODE=1 INCLUDE_PATH="$DEST/include" \
		LIBRARY_PATH="$DEST/lib" BINARY_PATH="$DEST/bin" LOC=-static-libgcc
	mkdir -p "$DEST/bin"
	cp -fp zlib1.dll "$DEST"/bin
	if [ -e "$DEST"/lib/pkgconfig/zlib.pc ]
	then
		# Relativize pkgconfig file of zlib 1.2.6+
		cat <<- PKGFIX > zlib.pc_
		prefix=$WPREFIX
		exec_prefix=\${prefix}
		libdir=\${exec_prefix}/lib
		sharedlibdir=\${libdir}
		includedir=\${prefix}/include
		
		`sed -e '/^Name:/,$!d' "$DEST"/lib/pkgconfig/zlib.pc`
		PKGFIX
		cp -fp zlib.pc_ "$DEST"/lib/pkgconfig/zlib.pc
	else
		# Rename import lib of zlib 1.2.5
		cp -fp libzdll.a "$DEST"/lib/libz.dll.a
	fi
	EXPORT
}

BUILD_xz ()
{
	UNPACK "xz-*.tar.*" || return 0
	PATH="$LONGPATH"
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-threads \
		--disable-nls --enable-small --disable-scripts LDFLAGS=-static-libgcc
	make
	make install-strip DESTDIR="$DESTDIR"
	RELATIVIZE "$DEST"/lib/pkgconfig/liblzma.pc
	EXPORT
}

BUILD_zstd ()
{
	UNPACK "zstd-*.tar.*" || return 0
	# Stop dragging C++ in by default
        sed -i '/^project(/ s/)/ C)/' build/cmake/CMakeLists.txt build/cmake/*/CMakeLists.txt
	patch -p1 <<- 'UNCXX' # Not attempt to mess with C++ cross-compiler
	--- zstd-1.4.5_/build/cmake/CMakeLists.txt	2020-05-22 08:04:00.000000000 +0300
	+++ zstd-1.4.5/build/cmake/CMakeLists.txt	2020-11-08 18:14:22.171222309 +0200
	@@ -40,12 +40,10 @@
	   set(PROJECT_VERSION_PATCH ${zstd_VERSION_PATCH})
	   set(PROJECT_VERSION "${zstd_VERSION_MAJOR}.${zstd_VERSION_MINOR}.${zstd_VERSION_PATCH}")
	   enable_language(C)   # Main library is in C
	-  enable_language(CXX) # Testing contributed code also utilizes CXX
	 else()
	   project(zstd
	     VERSION "${zstd_VERSION_MAJOR}.${zstd_VERSION_MINOR}.${zstd_VERSION_PATCH}"
	     LANGUAGES C   # Main library is in C
	-              CXX # Testing contributed code also utilizes CXX
	     )
	 endif()
	 message(STATUS "ZSTD VERSION: ${zstd_VERSION}")
	--- zstd-1.4.5_/build/cmake/CMakeModules/AddZstdCompilationFlags.cmake	2020-05-22 08:04:00.000000000 +0300
	+++ zstd-1.4.5/build/cmake/CMakeModules/AddZstdCompilationFlags.cmake	2020-11-08 18:17:48.056232126 +0200
	@@ -1,4 +1,3 @@
	-include(CheckCXXCompilerFlag)
	 include(CheckCCompilerFlag)
	 
	 function(EnableCompilerFlag _flag _C _CXX)
	@@ -12,18 +11,10 @@
	             set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_flag}" PARENT_SCOPE)
	         endif ()
	     endif ()
	-    if (_CXX)
	-        CHECK_CXX_COMPILER_FLAG(${_flag} CXX_FLAG_${varname})
	-        if (CXX_FLAG_${varname})
	-            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_flag}" PARENT_SCOPE)
	-        endif ()
	-    endif ()
	 endfunction()
	 
	 macro(ADD_ZSTD_COMPILATION_FLAGS)
	-    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MINGW) #Not only UNIX but also WIN32 for MinGW
	-        #Set c++11 by default
	-        EnableCompilerFlag("-std=c++11" false true)
	+    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR MINGW) #Not only UNIX but also WIN32 for MinGW
	         #Set c99 by default
	         EnableCompilerFlag("-std=c99" true false)
	         EnableCompilerFlag("-Wall" true true)
	@@ -56,8 +47,7 @@
	     # Remove duplicates compilation flags
	     foreach (flag_var CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
	              CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
	-             CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
	-             CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	+             )
	         if( ${flag_var} )
	             separate_arguments(${flag_var})
	             list(REMOVE_DUPLICATES ${flag_var})
	UNCXX
	patch -p1 <<- 'PKGC' # Install .pc
	--- a/build/cmake/lib/CMakeLists.txt
	+++ b/build/cmake/lib/CMakeLists.txt
	@@ -165,7 +165,7 @@ if (ZSTD_BUILD_STATIC)
	             OUTPUT_NAME ${STATIC_LIBRARY_BASE_NAME})
	 endif ()
	
	-if (UNIX)
	+if (UNIX OR MINGW)
	     # pkg-config
	     set(PREFIX "${CMAKE_INSTALL_PREFIX}")
	     set(LIBDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
	PKGC
	patch -p1 <<- 'DLLDIR' # Install .dll
	--- a/build/cmake/lib/CMakeLists.txt
	+++ b/build/cmake/lib/CMakeLists.txt
	@@ -163,6 +163,7 @@ install(TARGETS ${library_targets}
	     INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
	     ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	     LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	+    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	     )
	 
	 # uninstall target
	DLLDIR
	PATH="$LONGPATH"
	mkdir tbuild
	cd tbuild
	LDFLAGS=-static-libgcc \
	cmake -DCMAKE_TOOLCHAIN_FILE="$TOPDIR/toolchain" \
		-DZSTD_MULTITHREAD_SUPPORT=OFF -DZSTD_BUILD_STATIC=OFF \
		-DZSTD_BUILD_PROGRAMS=OFF -DCMAKE_INSTALL_PREFIX= \
		../build/cmake
	make
	make install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
	RELATIVIZE "$DEST"/lib/pkgconfig/libzstd.pc
	EXPORT
	echo 'DEV="$DEV lib/cmake/"' > "$DESTDIR.install"
}

BUILD_libjpeg ()
{
	UNPACK "jpegsrc.*.tar.*" || return 0
	PATH="$LONGPATH"
	./configure --prefix="$WPREFIX" --host=$MTARGET LDFLAGS=-static-libgcc
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

BUILD_libwebp ()
{
	UNPACK "libwebp-*.tar.*" || return 0
	PATH="$LONGPATH"
	NOCONFIGURE=1 ./autogen.sh
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-static \
		--disable-silent-rules --enable-everything --enable-swap-16bit-csp
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_libpng="zlib"
BUILD_libpng ()
{
	UNPACK "libpng-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-static
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
	# For those stupid things not using pkg-config
	local ZAD
	for ZAD in "$DEST"/bin/libpng*-config
	do
		ln -sf "${ZAD#$TOPDIR/}" "$TOPDIR/"
	done
	echo 'DEV="$DEV bin/libpng*-config"' > "$DESTDIR.install"
}

DEP_libtiff="zlib xz zstd libjpeg libwebp"
BUILD_libtiff ()
{
	UNPACK "tiff-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-cxx \
		--without-x
	make LDFLAGS="-XCClinker -static-libgcc -L$TOPDIR/lib"
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

BUILD_openjpeg ()
{
	UNPACK "openjpeg-*.tar.*" || return 0
	sed -i '/^project(/ s/)/ C)/' CMakeLists.txt
	PATH="$LONGPATH"
	mkdir build
	cd build
	LDFLAGS=-static-libgcc \
	cmake -DCMAKE_TOOLCHAIN_FILE="$TOPDIR/toolchain" \
		-DCMAKE_BUILD_TYPE=Release ..
	make install/strip DESTDIR="$DESTDIR"
	rm -rf "$DEST"/lib/openjpeg-* # Useless
	EXPORT
}

BUILD_lcms ()
{
	UNPACK "lcms2-*.tar.*" || return 0
	PATH="$LONGPATH"
	autoreconf -fi
	CPPFLAGS="-isystem $TOPDIR/include" \
	LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --disable-static
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

BUILD_bzip2 ()
{
	UNPACK "bzip2-*.tar.*" || return 0
	patch -p1 <<- 'FIX' # Fix the necessary minimum
	--- a/bzip2.c
	+++ b/bzip2.c
	@@ -128,7 +128,7 @@
	 #if BZ_LCCWIN32
	 #   include <io.h>
	 #   include <fcntl.h>
	-#   include <sys\stat.h>
	+#   include <sys/stat.h>
	 
	 #   define NORETURN       /**/
	 #   define PATH_SEP       '\\'
	--- a/bzlib.h
	+++ b/bzlib.h
	@@ -75,7 +75,7 @@ typedef
	 #include <stdio.h>
	 #endif
	 
	-#ifdef _WIN32
	+#if 0
	 #   include <windows.h>
	 #   ifdef small
	       /* windows.h define small to char */
	@@ -116,7 +116,7 @@ BZ_EXTERN int BZ_API(BZ2_bzCompressEnd) (
	 BZ_EXTERN int BZ_API(BZ2_bzDecompressInit) ( 
	       bz_stream *strm, 
	       int       verbosity, 
	-      int       small
	+      int       small_
	    );
	 
	 BZ_EXTERN int BZ_API(BZ2_bzDecompress) ( 
	@@ -140,7 +140,7 @@ BZ_EXTERN BZFILE* BZ_API(BZ2_bzReadOpen) (
	       int*  bzerror,   
	       FILE* f, 
	       int   verbosity, 
	-      int   small,
	+      int   small_,
	       void* unused,    
	       int   nUnused 
	    );
	@@ -216,7 +216,7 @@ BZ_EXTERN int BZ_API(BZ2_bzBuffToBuffDecompress) (
	       unsigned int* destLen,
	       char*         source, 
	       unsigned int  sourceLen,
	-      int           small, 
	+      int           small_, 
	       int           verbosity 
	    );
	 
	FIX
	PATH="$SHORTPATH"
	make libbz2.a
	cc *.o -shared -o libbz2.dll -Xlinker --out-implib -Xlinker libbz2.dll.a
	mkdir -p "$DEST/bin" "$DEST/lib" "$DEST/include"
	cp -a bzlib.h "$DEST/include/"
	cp -a libbz2*.a "$DEST/lib/"
	cp -a libbz2.dll "$DEST/bin/"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*
	EXPORT
}

do_BUILD_freetype ()
{
	sed -i 's/_pkg_min_version=.*/_pkg_min_version=0.23/' \
		builds/unix/configure # Does not need more
# !!! Cross-compile breaks down if cross-gcc named "gcc" is present in PATH
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-freetype-config --with-harfbuzz=$1
	make
	"$TARGET_STRIP" --strip-unneeded objs/.libs/*.dll objs/.libs/*.a
	make install DESTDIR="$DESTDIR"
	RELATIVIZE "$DEST"/lib/pkgconfig/freetype2.pc
	EXPORT
	# For those stupid things not using pkg-config
	ln -sf "${DEST#$TOPDIR/}"/bin/freetype-config "$TOPDIR/"
}

DEP_freetype_base="zlib libpng bzip2"
BUILD_freetype_base ()
{
	UNPACK "freetype-*.tar.*" _base || return 0
	do_BUILD_freetype no
	touch "$DESTDIR.exclude" # Do not package the contents
}

BUILD_libiconv ()
{
	UNPACK "libiconv-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-static
	make
	make install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*
	EXPORT
}

DEP_gettext="libiconv"
BUILD_gettext ()
{
	UNPACK "gettext-*.tar.*" || return 0
	cd gettext-runtime
	rm -f intl/canonicalize.[ch] intl/relocatex.[ch]
	patch -p1 < "$SRCDIR"/gettext1981runtime.patch
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-threads=win32 --enable-relocatable
	make -C intl
	make -C intl install DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
	EXPORT
}

BUILD_expat ()
{
	UNPACK "expat-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--without-docbook
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_fontconfig="freetype_base gettext expat"
BUILD_fontconfig ()
{
	UNPACK "fontconfig-*.tar.*" || return 0
	PATH="$LONGPATH"
	autoreconf -fi
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--with-arch=$MTARGET --disable-docs
	# !!! libtool eats the LDFLAGS, need to force 'em down its throat
	sed -i -e 's/-static-libgcc/-XCClinker -static-libgcc/' src/Makefile
	# "noinst_PROGRAMS=" is to avoid making tests, as test-hash.c is broken
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	EXPORT

	local ZAD
	for ZAD in "$DEST"/etc/fonts/conf.d/*.conf
	do
		rm "$ZAD"
		cp -a "$DEST/share/fontconfig/conf.avail/${ZAD##*/}" "$ZAD"
	done
	mkdir -p "$DEST"/var/cache/fontconfig # Just to be safe
	echo 'PKG="$PKG etc/ var/ share/fontconfig/"' > "$DESTDIR.install"
}

BUILD_pcre ()
{
	UNPACK "pcre-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-utf --enable-unicode-properties \
		--enable-pcre16 #--enable-pcre32 --enable-jit
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
	# For those stupid things not using pkg-config
	ln -sf "${DEST#$TOPDIR/}"/bin/pcre-config "$TOPDIR/"
	echo 'DEV="$DEV bin/pcre-config"' > "$DESTDIR.install"
}

BUILD_libffi ()
{
	UNPACK "libffi-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
}

DEP_glib="zlib libiconv gettext pcre libffi"
BUILD_glib ()
{
	UNPACK "glib-*.tar.*" || return 0
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/glib-2.0" "$TOPDIR/lib/glib-2.0"
	rm -rf "$TOPDIR/include/"gio*/ "$TOPDIR/lib/gio"
	local ZAD
	for ZAD in glib gmodule gobject gthread gio
	do
		rm -f "$TOPDIR/lib/pkgconfig"/$ZAD*.pc
		rm -f "$TOPDIR/lib"/lib$ZAD*.dll.a
	done
	PATH="$LONGPATH"
	NOCONFIGURE=1 ./autogen.sh
	CPPFLAGS="-fcommon -isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-compile-warnings \
		--disable-static --enable-debug=yes \
		--disable-gtk-doc-html --disable-man \
		--with-threads=win32 --with-pcre=system --with-libiconv=gnu
#		--disable-fam
	# "--disable-compile-warnings" as GCC 10 mistakes %I64u for %u
	make
	make install-strip DESTDIR="$DESTDIR"
	sed -i -e '/^Libs.private/s/-L[^ ]*//' "$DEST"/lib/pkgconfig/*.pc
	mv "$DEST/share/locale" "$DEST/lib/locale"
	echo 'PKG="$PKG bin/gspawn-win64-helper*.exe"' > "$DESTDIR.install"
	echo 'DEV="$DEV lib/glib-2.0/"' >> "$DESTDIR.install"
	EXPORT
}

DEP_pixman="libpng pthread"
BUILD_pixman ()
{
	UNPACK "pixman-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET
	# !!! libtool eats the LDFLAGS, need to force 'em down its throat
	sed -i -e 's/-static-libgcc/-XCClinker -static-libgcc/' pixman/Makefile
	# Avoiding making tests, which is simply too long
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	EXPORT
}

DEP_cairo="zlib libpng freetype_base fontconfig glib pixman"
BUILD_cairo ()
{
	UNPACK "cairo-*.tar.*" || return 0
	# _FORTIFY_SOURCE "support" in MinGW-w64 is such that it's much easier to
	# disable outright, than make behave
	sed -i -e '/_FORTIFY_SOURCE=2/d' configure
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--enable-win32 --enable-win32-font \
		--disable-xlib --disable-xcb --disable-quartz \
		--enable-tee \
		--enable-ps --enable-pdf --enable-svg \
		--disable-trace \
		--disable-gtk-doc --disable-gtk-doc-html
#		--disable-static
#		--disable-pthread --disable-atomic
	# !!! libtool eats the LDFLAGS, need to force 'em down its throat
	sed -i -e 's/-static-libgcc/-XCClinker -static-libgcc/' src/Makefile
	# Avoiding making tests & utils, which is simply too long
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	EXPORT
}

DEP_icu="libcxx"
BUILD_icu ()
{
	UNPACK "icu*.tar.*" || return 0
	local SDIR
	SDIR="$FDIR"/source
	[ -d "$SDIR" ] || SDIR="$FDIR"/icu4c/source
	# A native build is prerequisite for a cross-build
	local NATDIR
	NATDIR="$SDIR/zad"
	mkdir -p "$NATDIR"
	cd "$NATDIR"
	PATH="$DEFPATH"
	"$SDIR"/configure CC=${CC:-gcc} CXX=${CXX:-g++} \
		--disable-tests --disable-samples
	make
	cd "$SDIR"
	# Dynamic libstdc++ because static adds a few Mb extra
	# Dynamic libgcc because libstdc++ DLL will drag it in anyway
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--with-cross-build="$NATDIR" --enable-icu-config=no \
		CXXFLAGS='-std=c++11'
	make SO_TARGET_VERSION_SUFFIX=
	make install DESTDIR="$DESTDIR" SO_TARGET_VERSION_SUFFIX=
	# DLL install was into /lib as of v66.1, properly into /bin in 68.2
	mv -t "$DEST"/bin/ "$DEST"/lib/*[0-9].dll || true # Actual versioned DLLs
	rm -f "$DEST"/lib/*.dll # Unversioned symlinks are of no use
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll
	EXPORT
	echo 'DEV="$DEV lib/icu/"' > "$DESTDIR.install"
}

DEP_harfbuzz="freetype_base glib cairo icu pthread"
BUILD_harfbuzz ()
{
	UNPACK "harfbuzz-*.tar.*" || return 0
	PATH="$LONGPATH"
	NOCONFIGURE=1 ./autogen.sh
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		ac_cv_header_sys_mman_h=no CXXFLAGS='-std=c++11'
	# !!! Crazy libtool is crazy, excise shared libgcc from it by hand
	sed -i -e '/^postdeps=/s/-lgcc_s//g' libtool
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	RELATIVIZE "$DEST"/lib/pkgconfig/*.pc
	EXPORT
	echo 'DEV="$DEV lib/cmake/"' > "$DESTDIR.install"
}

DEP_freetype="$DEP_freetype_base harfbuzz"
BUILD_freetype ()
{
	UNPACK "freetype-*.tar.*" || return 0
	do_BUILD_freetype yes
	echo 'DEV="$DEV bin/freetype-config"' > "$DESTDIR.install"
}

# Needs the .xz/.bz2 release package, NOT the (halfbaked) source code .gz one
DEP_fribidi="glib"
BUILD_fribidi ()
{
	UNPACK "fribidi-*.tar.*" || return 0
	PATH="$LONGPATH"
	sed -i 's,__declspec(dllimport),,' lib/fribidi-common.h
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-debug --disable-deprecated
	make noinst_PROGRAMS= AM_DEFAULT_VERBOSITY=1
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	EXPORT
}

# !!! All MXE builds failed; MSYS2's 1.43.0 and Octave's 1.42.1 worked
# Maybe worth it to add libthai ? Get rid of fontconfig as in Octave?
DEP_pango="glib fontconfig cairo harfbuzz freetype fribidi"
BUILD_pango ()
{
	UNPACK "pango-*.tar.*" || return 0
	patch -p1 <<- 'ORDER'
	--- pango-1.40.3.orig/pango/Makefile.am	2019-01-25 08:50:50.848933403 -0500
	+++ pango-1.40.3/pango/Makefile.am	2019-01-25 08:52:32.440937173 -0500
	@@ -389,7 +389,7 @@
	 libpangowin32_1_0_la_LIBADD =			\
	 	libpango-$(PANGO_API_VERSION).la	\
	 	$(GLIB_LIBS)				\
	-	-lgdi32 -lusp10
	+	-lusp10 -lgdi32
	 libpangowin32_1_0_la_DEPENDENCIES =		\
	 	libpango-$(PANGO_API_VERSION).la
	 libpangowin32_1_0_la_SOURCES =	\
	--- pango-1.40.3.orig/pango/Makefile.in	2019-01-25 08:50:50.852933403 -0500
	+++ pango-1.40.3/pango/Makefile.in	2019-01-25 08:55:59.660944864 -0500
	@@ -867,7 +867,7 @@
	 libpangowin32_1_0_la_LIBADD = \
	 	libpango-$(PANGO_API_VERSION).la	\
	 	$(GLIB_LIBS)				\
	-	-lgdi32 -lusp10
	+	-lusp10 -lgdi32
	 
	 libpangowin32_1_0_la_DEPENDENCIES = libpango-$(PANGO_API_VERSION).la \
	 	$(am__append_33)
	--- pango-1.40.3.orig/pangowin32.pc.in	2019-01-25 08:50:50.848933403 -0500
	+++ pango-1.40.3/pangowin32.pc.in	2019-01-25 08:51:20.696934510 -0500
	@@ -8,5 +8,5 @@
	 Version: @VERSION@
	 Requires: pango
	 Libs: -L${libdir} -lpangowin32-@PANGO_API_VERSION@
	-Libs.private: -lgdi32 -lusp10
	+Libs.private: -lusp10 -lgdi32
	 Cflags: -I${includedir}/pango-1.0
	ORDER
	# !!! Remove traces of existing install - else confusion will result
	rm -rf "$TOPDIR/include/pango-1.0"
	rm -f "$TOPDIR/lib/pkgconfig"/pango*.pc "$TOPDIR/lib"/libpango*.dll.a
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc --disable-gtk-doc-html --disable-static \
		--enable-debug=yes
#		--disable-introspection
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	EXPORT
}

DEP_gdkpixbuf="libpng libjpeg libtiff libiconv gettext glib" # jasper
BUILD_gdkpixbuf ()
{
	UNPACK "gdk-pixbuf-*.tar.*" || return 0
	# Remove traces of existing install
	rm -rf "$TOPDIR/include/gdk-pixbuf*"
	rm -f "$TOPDIR/lib/pkgconfig"/gdk-pixbuf*.pc
	rm -f "$TOPDIR/lib"/libgdk_pixbuf*.dll.a
	WANT_JASPER=
	[ "`echo \"$TOPDIR\"/lib/libjasper*`" != "$TOPDIR/lib/libjasper*" ] && \
		WANT_JASPER=--with-libjasper
	PATH="$LONGPATH"
	CPPFLAGS="-fcommon -isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc --disable-gtk-doc-html --disable-static \
		--with-included-loaders --without-gdiplus $WANT_JASPER \
		--enable-relocations # For svg plugin
#		--disable-introspection
	make
	make install-strip DESTDIR="$DESTDIR"
	mv "$DEST/share/locale" "$DEST/lib/locale" # If at all
	EXPORT
}

DEP_libxml2="zlib xz libiconv"
BUILD_libxml2 ()
{
	UNPACK "libxml2-*.tar.*" || return 0
	sed -i 's,`uname`,MinGW,g' xml2-config.in
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--without-debug --without-python --without-threads
	# Clean up pkgconfig file
	sed -i '/^Libs.private:/s/ -L[^ ]\+//g' libxml-2.0.pc
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
	# For those stupid things not using pkg-config
	ln -sf "${DEST#$TOPDIR/}"/bin/xml2-config "$TOPDIR/"
	echo 'DEV="$DEV bin/xml2-config lib/cmake/"' > "$DESTDIR.install"
}

DEP_libcroco="glib libxml2"
BUILD_libcroco ()
{
	UNPACK "libcroco-*.tar.*" || return 0
	patch -p0 <<- 'LIBTOOL' # Make the outdated thing recognize 64-bit objects
	--- ltmain.sh0	2008-11-07 23:19:28.000000000 +0200
	+++ ltmain.sh	2020-12-22 19:56:21.038466138 +0200
	@@ -215,7 +215,7 @@
	     ;;
	   *ar\ archive*) # could be an import, or static
	     if eval $OBJDUMP -f $1 | $SED -e '10q' 2>/dev/null | \
	-      $EGREP -e 'file format pe-i386(.*architecture: i386)?' >/dev/null ; then
	+      $EGREP 'file format (pei*-i386(.*architecture: i386)?|pe-arm-wince|pe-x86-64)' >/dev/null; then
	       win32_nmres=`eval $NM -f posix -A $1 | \
	 	$SED -n -e '1,100{
	 		/ I /{
	LIBTOOL
	PATH="$ALLPATH" # Else libtool runs host objdump
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc
	make
	make install-strip DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/*.dll # The install-strip doesn't
	EXPORT
}

DEP_libgsf="zlib bzip2 gettext glib libxml2"
BUILD_libgsf ()
{
	UNPACK "libgsf-*.tar.*" || return 0
	# Fix pkgconfig file
	sed -i 's/^Requires:.*/& gio-2.0/' libgsf-1.pc.in
	echo 'Libs.private: -lz -lbz2' >> libgsf-1.pc.in
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc --with-zlib --with-bz2
	make -C gsf
	make -C gsf install-strip DESTDIR="$DESTDIR"
	make install-pkgconfigDATA DESTDIR="$DESTDIR"
	rm -f "$DEST"/lib/pkgconfig/libgsf-win32*.pc # No such thing built
	EXPORT
}

BROKEN_SVG_PLUGIN=1 # !!! Fails to do its job, for some reason

DEP_librsvg="glib libxml2 cairo pango gdkpixbuf libcroco libgsf"
BUILD_librsvg ()
{
	UNPACK "librsvg-*.tar.*" || return 0
	sed -i 's/ gio-unix/ gio-windows/g' configure # Replace dependency
	patch -p1 <<- 'GIO' # commit 1811f207653e36c301260203866d958e443b2150
	--- a/rsvg-convert.c
	+++ b/rsvg-convert.c
	@@ -36,7 +36,11 @@
	 #include <locale.h>
	 #include <glib/gi18n.h>
	 #include <gio/gio.h>
	+#ifdef _WIN32
	+#include <gio/gwin32inputstream.h>
	+#else
	 #include <gio/gunixinputstream.h>
	+#endif
	 
	 #include "rsvg-css.h"
	 #include "rsvg.h"
	@@ -213,7 +217,11 @@ main (int argc, char **argv)
	 
	         if (using_stdin) {
	             file = NULL;
	+#ifdef _WIN32
	+            stream = g_win32_input_stream_new (STDIN_FILENO, FALSE);
	+#else
	             stream = g_unix_input_stream_new (STDIN_FILENO, FALSE);
	+#endif
	         } else {
	             GFileInfo *file_info;
	             gboolean compressed = FALSE;
	GIO
	patch -p0 <<- 'REALPATH' # Substitute; adapted from bug #710163
	--- rsvg-base.c0	2014-06-18 21:05:08.000000000 +0300
	+++ rsvg-base.c	2020-12-22 23:04:56.777290547 +0200
	@@ -57,6 +57,8 @@
	 #include "rsvg-paint-server.h"
	 #include "rsvg-xml.h"
	 
	+#define realpath(a,b) _fullpath(b,a,_MAX_PATH)
	+
	 /*
	  * This is configurable at runtime
	  */
	REALPATH
	local gdk_pixbuf_binarydir
	local gdk_pixbuf_moduledir
	local gdk_pixbuf_cache_file
	gdk_pixbuf_binarydir="`$PKG_CONFIG --variable=gdk_pixbuf_binarydir gdk-pixbuf-2.0`"
	gdk_pixbuf_moduledir="`$PKG_CONFIG --variable gdk_pixbuf_moduledir gdk-pixbuf-2.0`"
	gdk_pixbuf_cache_file="`$PKG_CONFIG --variable gdk_pixbuf_cache_file gdk-pixbuf-2.0`"
	# Make configure script use prepared (de-prefixed) values of these
	sed -i '/gdk_pixbuf_\(binarydir\|moduledir\|cache_file\)=/d' configure
	PATH="$LONGPATH"
	gdk_pixbuf_binarydir="${gdk_pixbuf_binarydir#$TOPDIR}" \
	gdk_pixbuf_moduledir="${gdk_pixbuf_moduledir#$TOPDIR}" \
	gdk_pixbuf_cache_file="${gdk_pixbuf_cache_file#$TOPDIR}" \
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
	        --disable-gtk-doc --disable-tools --enable-introspection=no
	make
	make install-strip DESTDIR="$DESTDIR"
	[ "${BROKEN_SVG_PLUGIN:-0}" -ne 0 ] && rm -f "$DEST${gdk_pixbuf_moduledir#$TOPDIR}"/*
	EXPORT
	echo 'PKG="$PKG bin/rsvg-convert.exe '"${gdk_pixbuf_moduledir#$TOPDIR/}"'/*.dll"' > "$DESTDIR.install"
}

DEP_atk="gettext glib"
BUILD_atk ()
{
	UNPACK "atk-*.tar.*" || return 0
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/atk-1.0"
	rm -f "$TOPDIR/lib/pkgconfig"/atk.pc "$TOPDIR/lib"/libatk*.dll.a
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc --disable-gtk-doc-html --disable-static
#		--disable-introspection
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	mv "$DEST/share/locale" "$DEST/lib/locale"
	EXPORT
}

DEP_gtk="gettext glib cairo pango gdkpixbuf atk"
BUILD_gtk ()
{
	UNPACK "gtk+-2*.tar.*" || return 0
	patch -p1 -i "$SRCDIR/gtk22429_1wj.patch"
	rm gtk/gtk.def # Force it be recreated
	# !!! Remove traces of existing install - else confusion may result
	rm -rf "$TOPDIR/include/gtk-2.0" "$TOPDIR/include/gail-1.0" "$TOPDIR/lib/gtk-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/gail.pc "$TOPDIR/lib/pkgconfig"/gdk-[2w]*.pc \
		"$TOPDIR/lib/pkgconfig"/gtk*.pc \
		"$TOPDIR/lib"/libg[dt]k-*.dll.a "$TOPDIR/lib"/libgailutil.dll.a
	PATH="$LONGPATH"
	CPPFLAGS="-fcommon -isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		--disable-gtk-doc --disable-gtk-doc-html --disable-static \
		--enable-debug=yes --with-included-immodules --with-gdktarget=win32 \
		--disable-glibtest --disable-modules --disable-cups --disable-papi
#		--disable-introspection
	make noinst_PROGRAMS=
	make install-strip DESTDIR="$DESTDIR" noinst_PROGRAMS=
	find "$DEST/lib/gtk-2.0/" \( -name '*.dll.a' -o -name '*.la' \) -delete
	mv "$DEST/share/locale" "$DEST/lib/locale"
	rm -f "$DEST/lib/locale"/*/LC_MESSAGES/gtk20-properties.mo
#	cp -p "$SRCDIR/gtkrc" "$DEST/etc/gtk-2.0" || true
	EXPORT
	printf '%s%s\n' 'PKG="$PKG lib/gtk-2.0/*/*/*.dll ' \
		'lib/gtk-2.0/*/*.dll share/themes/ etc/"' > "$DESTDIR.install"
	echo 'DEV="$DEV lib/gtk-2.0/include/"' >> "$DESTDIR.install"
}

DEP_gifsicle="pthread"
BUILD_gifsicle ()
{
	UNPACK "gifsicle-*.tar.*" || return 0
	PATH="$LONGPATH"
	# !!! Uncomment the below for buggy GCC 10.2
#	CFLAGS="-O2 -fno-ipa-cp" \
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
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
	UNPACK "mtpaint-*.tar.*" || return 0
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		release intl
	make
	make install DESTDIR="$DESTDIR"
	# Now prepare Windows-specific package parts
	local ZAD
	for ZAD in COPYING NEWS README
	do
		# Convert to CRLF
		sed -e 's/$/\x0D/g' ./$ZAD >"$DEST/$ZAD.txt"
	done
	cp -a -t "$DEST" "$SRCDIR/"*.ico
	ZAD="${DESTDIR##*mtpaint-}" # Version number
	sed -e "s/%VERSION%/$ZAD/g" "$SRCDIR/mtpaint-setup.iss" \
		> "$DEST/mtpaint-setup.iss"
	ZAD='[InternetShortcut]\r\nURL=http://mtpaint.sourceforge.net/\r\n'
	echo -en "$ZAD" > "$DEST/mtpaint.url"
	echo 'PKG="./"' > "$DESTDIR.install"
	echo 'DEV=' >> "$DESTDIR.install"
}

BUILD_mtpaint_handbook ()
{
	UNPACK "mtpaint_handbook-*.zip" || return 0
	mkdir -p "$DEST"
	cp -ar -t "$DEST" docs
	echo 'PKG="docs/"' > "$DESTDIR.install"
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

	# Add loader cache file about SVG plugin - IF the plugin is working
	if [ "${BROKEN_SVG_PLUGIN:-0}" -eq 0 ]
	then
		ZAD="`$PKG_CONFIG --variable gdk_pixbuf_cache_file gdk-pixbuf-2.0`"
		ZAD="$PKGDIR${ZAD#$TOPDIR}"
		PERED="$SRCDIR/${ZAD##*/}"
		DIR="`$PKG_CONFIG --variable gdk_pixbuf_moduledir gdk-pixbuf-2.0`"
		DIR="$PKGDIR${DIR#$TOPDIR}"
		mkdir -p "${ZAD%/*}"
		if [ -f "$PERED" ] # Have prepared cache file
		then
			cp -p "$PERED" "$ZAD"
		elif which wine >/dev/null # Can prepare it using Wine
		then
			( cd "$PKGDIR$WPREFIX/bin" && \
			wine "$INSDIR"/gdk-pixbuf*"$WPREFIX"/bin/gdk-pixbuf-query-loaders.exe "$DIR"/*.dll \
			| sed -e '/\.dll"/s|"'$PKGDIR$WPREFIX/'|"|' > "$ZAD" )
		fi
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

BUILD_dev ()
{
	UNPACK "dev.tar.*" || return 0
	touch "$DESTDIR.exclude" # Do not package the contents
	ln -sf "$FDIR" "$DEST"
	EXPORT
}

BUILD_vars ()
{
	cat <<- VARS > "$TOPDIR/vars"
	MPREFIX='$MPREFIX'
	MTARGET=$MTARGET
	TOPDIR='$TOPDIR'
	$ALLPATHS
	PATH="\$LONGPATH"
	CPPFLAGS="-isystem \$TOPDIR/include"
	LDFLAGS="-static-libgcc -L\$TOPDIR/lib"
	PKG_CONFIG="\$TOPDIR${PKG_CONFIG#$TOPDIR}"
	VARS
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
test -d "$TOPDIR/include" || cp -sR "$MPREFIX/$MTARGET/include/" "$TOPDIR/include/"
test -d "$TOPDIR/lib" || cp -sR "$MPREFIX/$MTARGET/lib/" "$TOPDIR/lib/"

# Create links for what misconfigured cross-compilers fail to provide
mkdir -p "$TOPDIR/bin"
LONGCC=`PATH="$LONGPATH" which $MTARGET-gcc`
for BINARY in ${LONGCC%/*}/$MTARGET-*
do
	ln -sf "$BINARY" "$TOPDIR/bin/${BINARY##*/$MTARGET-}"
done
[ -x "$TOPDIR/bin/cc" ] || ln -sf gcc "$TOPDIR/bin/cc"

# Fix bad float.h
if ! ( cd "$TOPDIR" && bin/cc -isystem include -imacros float.h -dM -E - < /dev/null | \
	grep -E '\bDBL_EPSILON\b' > /dev/null )
then
	cp -p "$TOPDIR/include/float.h" "$TOPDIR/include/float.h.new"
	patch -d "$TOPDIR/include" -p3 <<- 'FLOATFIX'
	--- a/mingw-w64-headers/crt/float.h.new
	+++ b/mingw-w64-headers/crt/float.h.new
	@@ -114,6 +114,15 @@
	 	#define DBL_MAX_10_EXP	__DBL_MAX_10_EXP__
	 	#define LDBL_MAX_10_EXP	__LDBL_MAX_10_EXP__
	 
	+    /* The difference between 1 and the least value greater than 1 that is
	+    representable in the given floating point type, b**1-p.  */
	+    #undef FLT_EPSILON
	+    #undef DBL_EPSILON
	+    #undef LDBL_EPSILON
	+    #define FLT_EPSILON __FLT_EPSILON__
	+    #define DBL_EPSILON __DBL_EPSILON__
	+    #define LDBL_EPSILON    __LDBL_EPSILON__
	+
	 	/* Addition rounds to 0: zero, 1: nearest, 2: +inf, 3: -inf, -1: unknown.  */
	 	/* ??? This is supposed to change with calls to fesetround in <fenv.h>.  */
	 	#undef FLT_ROUNDS
	FLOATFIX
	ln -sf float.h.new "$TOPDIR/include/float.h"
fi

# Prepare traps for stupid configure scripts
for SCRIPT in libpng-config pcre-config freetype-config xml2-config
do
	[ -x "$TOPDIR/$SCRIPT" ] || ln -s `which false` "$TOPDIR/$SCRIPT"
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

# Prepare cmake toolchain config
cat << TOOLCHAIN > "$TOPDIR/toolchain"
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CROSS_COMPILING ON)
set(CMAKE_C_COMPILER "$MPREFIX/bin/$MTARGET-gcc")
set(CMAKE_RC_COMPILER "$MPREFIX/bin/$MTARGET-windres")
set(CMAKE_FIND_ROOT_PATH "$TOPDIR" "$MPREFIX")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_INSTALL_PREFIX "${WPREFIX:-/}" CACHE PATH "")

option(BUILD_PKGCONFIG_FILES "" ON)
TOOLCHAIN

# A little extra safety
LIBS= PROGRAMS= PHONY= TOOLS=

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
