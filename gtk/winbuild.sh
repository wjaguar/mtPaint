#!/bin/sh
# winbuild.sh - cross-compile GTK+ and its dependencies for Windows

# Copyright (C) 2010,2011,2017,2019,2020,2021 Dmitry Groshev

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

LIBS="zlib xz zstd libjpeg libwebp libpng libtiff freetype openjpeg lcms "\
"libiconv gettext glib atk pango gtk"
PROGRAMS="gifsicle mtpaint mtpaint_handbook"
PHONY="libs all"
TOOLS="dev vars"
LOCALES="es cs fr pt pt_BR de pl tr zh_TW sk zh_CN ja ru gl nl it sv tl hu"
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

# MXE
# http://mxe.cc/
#MPREFIX=~/mxe/usr
#MTARGET=i686-w64-mingw32.shared

ALLPATHS='
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
	*.tar.bz2) FDIR=.tar.bz2 ; FCMD="tar $2 -xf" ;;
	*.tar.gz)  FDIR=.tar.gz ; FCMD="tar $2 -xf" ;;
	*.tar.xz)  FDIR=.tar.xz ; FCMD="tar --use-compress-program xz -xf" ;;
	*.tar.zst) FDIR=.tar.zst ; FCMD="tar --use-compress-program zstd -xf" ;;
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

# Relativize .pc file
RELATIVIZE ()
{
	if grep 'exec_prefix=\$' "$1"
	then :
	else # Relativize pkgconfig file
		cat <<- PKGFIX > "$1"_
		prefix=$WPREFIX
		exec_prefix=\${prefix}
		libdir=\${exec_prefix}/lib
		includedir=\${prefix}/include
		
		`sed -e '/^Name:/,$!d' "$1"`
		PKGFIX
		cp -fp "$1"_ "$1"
	fi
}

# Tools
TARGET_STRIP="$MPREFIX/bin/$MTARGET-strip"

BUILD_zlib ()
{
	UNPACK "zlib-*.tar.*" || return 0
	PATH="$SHORTPATH"
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
	# !!! Binutils 2.20 cannot create proper export libs if given a version
	# script; so any libpng version before 1.4.4 needs the below fix
	sed -i 's/have_ld_version_script=yes/have_ld_version_script=no/' ./configure
	# !!! Make libpng12 default DLL, same as it is default everything else
	sed -i 's/for ext in a la so sl/for ext in a dll.a la so sl/' ./Makefile.in
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --without-libpng-compat
	make
	make install-strip DESTDIR="$DESTDIR"
	EXPORT
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

DEP_freetype="zlib libpng"
BUILD_freetype ()
{
	UNPACK "freetype-*.tar.*" || return 0
	sed -i 's/_pkg_min_version=.*/_pkg_min_version=0.23/' \
		builds/unix/configure # Does not need more
# !!! Cross-compile breaks down if cross-gcc named "gcc" is present in PATH
	PATH="$LONGPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET
	make
	"$TARGET_STRIP" --strip-unneeded objs/.libs/*.dll objs/.libs/*.a
	make install DESTDIR="$DESTDIR"
	if grep 'exec_prefix=\$' "$DEST"/lib/pkgconfig/freetype2.pc
	then :
	else
		# Relativize pkgconfig file of FreeType 2.4.12+
		cat <<- PKGFIX > freetype2.pc_
		prefix=$WPREFIX
		exec_prefix=\${prefix}
		libdir=\${exec_prefix}/lib
		includedir=\${prefix}/include
		
		`sed -e '/^Name:/,$!d' "$DEST"/lib/pkgconfig/freetype2.pc`
		PKGFIX
		cp -fp freetype2.pc_ "$DEST"/lib/pkgconfig/freetype2.pc
	fi
	EXPORT
}

: << 'COMMENTED_OUT'
DEP_jasper="libjpeg"
# !!! Versions 2.x not supported yet !!!
BUILD_jasper ()
{
	UNPACK "jasper-1.*.tar.*" || return 0
	sed -i '/libjasper_la_LDFLAGS/ s/=/= -no-undefined/' src/libjasper/Makefile.in
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
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-shared \
		lt_cv_deplibs_check_method='match_pattern \.dll\.a$'
	# !!! Otherwise it tests for PE file format, which naturally fails
	# if no .la file is present to provide redirection
	make LDFLAGS="-XCClinker -static-libgcc -L$TOPDIR/lib"
	make install-strip DESTDIR="$DESTDIR"
	"$TARGET_STRIP" --strip-unneeded "$DEST"/bin/libjasper*.dll
	EXPORT
}
COMMENTED_OUT

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
	rm -rf "$DESTDIR"/lib/openjpeg-* # Useless
	EXPORT
}

BUILD_lcms ()
{
	UNPACK "lcms2-*.tar.*" || return 0
	PATH="$LONGPATH"
	# The '-D...' and 'ax_cv_...' are workarounds for config bugs in 2.9
	CPPFLAGS="-isystem $TOPDIR/include -DCMS_RELY_ON_WINDOWS_STATIC_MUTEX_INIT" \
	LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	./configure --prefix="$WPREFIX" --host=$MTARGET \
		ax_cv_have_func_attribute_visibility=no \
		ax_cv_check_cflags___fvisibility_hidden=no
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
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
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
	@@ -608,10 +608,10 @@
	 # define SUBLANG_SERBIAN_CYRILLIC 0x03
	 # endif
	 # ifndef SUBLANG_SINDHI_INDIA
	-# define SUBLANG_SINDHI_INDIA 0x00
	+# define SUBLANG_SINDHI_INDIA 0x01
	 # endif
	 # ifndef SUBLANG_SINDHI_PAKISTAN
	-# define SUBLANG_SINDHI_PAKISTAN 0x01
	+# define SUBLANG_SINDHI_PAKISTAN 0x02
	 # endif
	 # ifndef SUBLANG_SPANISH_GUATEMALA
	 # define SUBLANG_SPANISH_GUATEMALA 0x04
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
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
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
	patch -p1 <<- 'END' # Is needed to fix build failure with gcc 4.3+
	diff -udr glib-2.6.6/glib/gutils.h glib-2.6.6_/glib/gutils.h
	--- glib-2.6.6/glib/gutils.h	2005-04-19 12:06:05.000000000 +0400
	+++ glib-2.6.6_/glib/gutils.h	2017-10-10 02:35:44.765808857 +0300
	@@ -97,7 +97,7 @@
	 #  define G_INLINE_FUNC
	 #  undef  G_CAN_INLINE
	 #elif defined (__GNUC__) 
	-#  define G_INLINE_FUNC extern inline
	+#  define G_INLINE_FUNC static inline
	 #elif defined (G_CAN_INLINE) 
	 #  define G_INLINE_FUNC static inline
	 #else /* can't inline */
	END
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
	@@ -530,10 +530,10 @@ g_win32_ftruncate (gint  fd,
	 #define SUBLANG_SERBIAN_CYRILLIC 0x03
	 #endif
	 #ifndef SUBLANG_SINDHI_INDIA
	-#define SUBLANG_SINDHI_INDIA 0x00
	+#define SUBLANG_SINDHI_INDIA 0x01
	 #endif
	 #ifndef SUBLANG_SINDHI_PAKISTAN
	-#define SUBLANG_SINDHI_PAKISTAN 0x01
	+#define SUBLANG_SINDHI_PAKISTAN 0x02
	 #endif
	 #ifndef SUBLANG_SPANISH_GUATEMALA
	 #define SUBLANG_SPANISH_GUATEMALA 0x04
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
	END
	# Remove traces of existing install - just to be on the safe side
	rm -rf "$TOPDIR/include/glib-2.0" "$TOPDIR/lib/glib-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/glib*.pc "$TOPDIR/lib/pkgconfig"/gmodule*.pc
	rm -f "$TOPDIR/lib/pkgconfig"/gobject*.pc "$TOPDIR/lib/pkgconfig"/gthread*.pc
	rm -f "$TOPDIR/lib"/libglib*.dll.a "$TOPDIR/lib"/libgmodule*.dll.a
	rm -f "$TOPDIR/lib"/libgobject*.dll.a "$TOPDIR/lib"/libgthread*.dll.a
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	CFLAGS="-mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no \
		--with-threads=win32 glib_cv_stack_grows=no
# !!! See about "-fno-strict-aliasing": is it needed here with GCC 4, or not?
	make LDFLAGS="-XCClinker -static-libgcc -L$TOPDIR/lib"
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
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	CFLAGS="-mms-bitfields $OPTIMIZE" \
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
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-static-libgcc -L$TOPDIR/lib" \
	CFLAGS="-mms-bitfields $OPTIMIZE" \
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
	patch -p1 -i "$SRCDIR/gtk267_7wj.patch"
	# !!! Remove traces of existing install - else confusion will result
	rm -rf "$TOPDIR/include/gtk-2.0" "$TOPDIR/lib/gtk-2.0"
	rm -f "$TOPDIR/lib/pkgconfig"/g[dt]k*.pc "$TOPDIR/lib"/libg[dt]k*.dll.a
	PATH="$ALLPATH"
	CPPFLAGS="-isystem $TOPDIR/include" LDFLAGS="-L$TOPDIR/lib" \
	CFLAGS="-mms-bitfields $OPTIMIZE" \
	./configure --prefix="$WPREFIX" --host=$MTARGET --enable-all-warnings \
		--disable-static --disable-gtk-doc --enable-debug=yes \
		--enable-explicit-deps=no \
		--with-gdktarget=win32 --with-wintab="$WTKIT"
	make LDFLAGS="-XCClinker -static-libgcc -L$TOPDIR/lib"
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
	UNPACK "mtpaint-*.tar.bz2" || return 0
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
		sed -e 's/$/\x0D/g' ./$ZAD >"$DESTDIR/$ZAD.txt"
	done
	cp -a -t "$DESTDIR" "$SRCDIR/"*.ico
	ZAD="${DESTDIR##*mtpaint-}" # Version number
	sed -e "s/%VERSION%/$ZAD/g" "$SRCDIR/mtpaint-setup.iss" \
		> "$DESTDIR/mtpaint-setup.iss"
	ZAD='[InternetShortcut]\r\nURL=http://mtpaint.sourceforge.net/\r\n'
	echo -en "$ZAD" > "$DESTDIR/mtpaint.url"
	echo 'PKG="./"' > "$DESTDIR.install"
	echo 'DEV=' >> "$DESTDIR.install"
}

BUILD_mtpaint_handbook ()
{
	UNPACK "mtpaint_handbook-*.zip" || return 0
	mkdir -p "$DESTDIR"
	cp -ar -t "$DESTDIR" docs
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

BUILD_dev ()
{
	UNPACK "dev.tar.*" || return 0
	touch "$DEST.exclude" # Do not package the contents
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

# Provide glib-genmarshal wrapper
GLIB_GENMARSHAL="$TOPDIR/$MTARGET-glib-genmarshal"
export GLIB_GENMARSHAL
rm -f "$GLIB_GENMARSHAL"
cat << GENMARSHAL > "$GLIB_GENMARSHAL"
#!/bin/sh
glib-genmarshal "\$@" | sed 's/g_value_get_schar/g_value_get_char/'
GENMARSHAL
chmod +x "$GLIB_GENMARSHAL"
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
