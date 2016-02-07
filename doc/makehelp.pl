#! /usr/bin/perl
# Script to generate README, help.ccc & POD for mtPaint tarball, then HTML for website
# Written for Bash by Mark Tyler, 14-10-2004
# Rewritten in Perl by Dmitry Groshev, 20-04-2007
# New help.c file format implemented, 12-05-2007
# Stored source textfiles inline, 14-01-2009
# Pared down to generating only help.c with README & HELP as source, 07-02-2016

# ============================================================================
$HELP = <<"HELPFILE";
/*	help.c
	Copyright (C) 2004-$WHEN Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#undef _
#define _(X) X

#define HELP_PAGE_COUNT $HELP_PAGE_COUNT
static char *help_titles[HELP_PAGE_COUNT] = {
$help_titles};

$help_pages
#define HELP_PAGE_MAX $HELP_PAGE_MAX

static char **help_pages[HELP_PAGE_COUNT] = {
	$help_page_names
};

#undef _
#define _(X) __(X)
HELPFILE

INIT {
	# Read in ../README
	@parts = split /^---.*\n/m, `cat ../README`;
	s/\n+$/\n/s foreach @parts;
	for ($i = 0; $i < @parts; $i++)
	{
		last if $parts[$i] =~ /^Credits/;
	}
	$parts[1] =~ /2004-(\d{4})/ or die "Lost in time";
	$WHEN = $1;

	# Read in ./HELP
	@help = split /^---.*\n/m, `cat ./HELP`;
	s/\n+$/\n/s foreach @help;

	# Generate help parts
	@inf = ( "General\n\n" . $parts[1] . $parts[2],
		$help[1] . $help[2],
		$help[3] . $help[4],
		$parts[$i] . $parts[$i + 1] );
	s/\r//g foreach @inf; # To be sure

	$HELP_PAGE_COUNT = @inf;
	$i = 0;
	foreach (@inf)
	{
		/^([^\n]*)/;
		$help_titles .= "_(\"$1\"),\n";
		$help_page_names .= "help_page$i, ";
		$help_pages .= "static char *help_page$i\[] = {\n";
		$i++;
		s/([\\"])/\\$1/g; # Quote
		s/\n(?=\n)/\\n/g;
		@lines = split /\n/s;
		shift @lines; # Drop header
		$max = @lines unless $max > @lines;
		$help_pages .= "_(\"$_\"),\n" foreach @lines;
		$help_pages .= "NULL };\n";
	}
	$HELP_PAGE_MAX = $max + 1;
}

open HELP, ">help.c";
binmode HELP;
print HELP $HELP;
close HELP;

#`mv -f help.c ../src`;
#`chmod a-w ../src/help.c`;
