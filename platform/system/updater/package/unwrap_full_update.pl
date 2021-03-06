#!/usr/bin/perl -w
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# This tool unpacks a full update package generated by make_full_update.sh
# Author: Benjamin Smedberg
#

# -----------------------------------------------------------------------------
# By default just assume that these tools exist on our path

use Getopt::Std;

my ($MAR, $XZ, $BZIP2, $MAR_OLD_FORMAT, $archive, @marentries, @marfiles);

if (defined($ENV{"MAR"})) {
    $MAR = $ENV{"MAR"};
}
else {
    $MAR = "mar";
}

if (defined($ENV{"BZIP2"})) {
    $BZIP2 = $ENV{"BZIP2"};
}
else {
    $BZIP2 = "bzip2";
}

if (defined($ENV{"XZ"})) {
    $XZ = $ENV{"XZ"};
}
else {
    if (system("xz --version > /dev/null 2>&1") != 0) {
        die("The xz executable must be in the path.");
    }
    else {
        $XZ = "xz";
    }
}

sub print_usage
{
    print "Usage: unwrap_full_update.pl [OPTIONS] ARCHIVE\n\n";
    print "The contents of ARCHIVE will be unpacked into the current directory.\n\n";
    print "Options:\n";
    print "  -h show this help text\n";
}

my %opts;
getopts("h", \%opts);

if (defined($opts{'h'}) || scalar(@ARGV) != 1) {
    print_usage();
    exit 1;
}

$archive = $ARGV[0];
@marentries = `"$MAR" -t "$archive"`;

$? && die("Couldn't run \"$MAR\" -t");

if (system("$MAR -x \"$archive\"") != 0) {
  die "Couldn't run $MAR -x";
}

# Try to determine if the mar file contains bzip2 compressed files and if not
# assume that the mar file contains lzma compressed files. The updatev3.manifest
# file is checked since a valid mar file must have this file in the root path.
open(my $testfilename, "updatev3.manifest") or die $!;
binmode($testfilename);
read($testfilename, my $bytes, 3);
if ($bytes eq "BZh") {
    $MAR_OLD_FORMAT = 1;
} else {
    undef $MAR_OLD_FORMAT;
}
close $testfilename;

shift @marentries;

foreach (@marentries) {
    tr/\n\r//d;
    my @splits = split(/\t/,$_);
    my $file = $splits[2];

    print "Decompressing: " . $file . "\n";
    if ($MAR_OLD_FORMAT == 1) {
      system("mv \"$file\" \"$file.bz2\"") == 0 ||
        die "Couldn't mv \"$file\"";
      system("\"$BZIP2\" -d \"$file.bz2\"") == 0 ||
        die "Couldn't decompress \"$file\"";
    }
    else {
      system("mv \"$file\" \"$file.xz\"") == 0 ||
        die "Couldn't mv \"$file\"";
      system("\"$XZ\" -d \"$file.xz\"") == 0 ||
        die "Couldn't decompress \"$file\"";
    }
}

print "Finished\n";
