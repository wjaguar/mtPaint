#! /usr/bin/perl

# Usage: xtend infile.po outfile.po

undef $/;
$po = <>;

while ($po =~ m!msgid\s+"(/[^/"]+)"\s*msgstr\s+"(/[^/"]+)"!gsx)
{
	$tmp = $1; $tm2 = $2;
	$tmp =~ s/_//; $tm2 =~ s/_//;
	next if ($tmp eq $tm2);
	$menu{$tmp} = $tm2;
}

$po =~ s!msgid\s+"(/[^/"]+)(/[^"]+)"\s*msgstr\s+""!
	"msgid \"$1$2\"\nmsgstr \"" .
	(defined($menu{$1}) ? $menu{$1} . $2 : "") . "\""
!egsx;

open OUT, ">", $ARGV[0];
binmode OUT;
print OUT $po;
