#! /usr/bin/env perl

=head1 DESCRIPTION

Generates static hash table of commands by parsing C source.

=cut

use strict;
use warnings;

my %consts;

while (<STDIN>) {
	$_ =~ /(\w+)/ and $consts{$1}= 1;
}
my @constlist= sort keys %consts;

# table size is 2 x number of entries rounded up to power of 2.
my $mask= int(2 * @constlist);
$mask |= $mask >> 1;
$mask |= $mask >> 2;
$mask |= $mask >> 4;
$mask |= $mask >> 8;
$mask |= $mask >> 16;
my $scan_dist= 4;
my $table_size= $mask+1+$scan_dist;

sub build_table {
	my ($mul, $shift)= @_;
	my @table= (0) x $table_size;
	name: for my $ci (0..$#constlist) {
		my $bucket= hash_fn($constlist[$ci], $mul, $shift);
		for (0..$scan_dist) {
			if (!$table[$bucket+$_]) {
				$table[$bucket+$_]= $ci+1; # 1-based index
				next name;
			}
		}
		return undef;
	}
	return \@table;
}

sub find_collisionless_hash_params {
	# pick factors for the hash function until each command has a unique bucket
	for (my $mul= 1; $mul < $table_size*$table_size; $mul++) {
		for (my $shift= 1; $shift < 5; $shift++) {
			my $table= build_table($mul, $shift);
			return ( $table, $mul, $shift )
				if $table;
		}
	}
	die "No value of \$shift / \$mul results in unique codes for each command\n";
}

my ($table, $mul, $shift)= find_collisionless_hash_params();

my $i= 1;
my $const_entries= join("\n", map sprintf(' /* %4d */ { "%s", (long)(%s) },', $i++, $_, $_), @constlist);

my $hash_entries= '';
for (0..$#$table) {
	$hash_entries .= "\n" if $_ && !($_ & 0xF);
	$hash_entries .= sprintf(" %4d,", $table->[$_]);
}

# This must be kept in sync with the C version.
# Use a mask similar to a 32-bit register's effect to make sure Perl
# behaves the same way as C regardless of Perl's integer width, and
# then tell C to explicitly use 32-bit math.
sub hash_fn {
	my ($string, $mul, $shift)= @_;
	use integer;
	my $i32_mask= (1<<(32-$shift))-1;
	my $result= 0;
	$result= ((($result * $mul) >> $shift) & $i32_mask) + ($_ << 4)
		for unpack( 'C' x length($string), $string );
	return $result & $mask;
}

print <<END;
// File generated by $0
//
// ${\scalar @constlist} constants
// table size is $table_size, mul is $mul, shift is $shift, scan_dist is $scan_dist
#define INCLUDE_SDL
#define INCLUDE_GL
#include "config.h"
#include "ProcessInput.h"
#include "SymbolHash.h"

int IntConstHashFunc(const char *name) {
	uint32_t x= 0;
	while (*name)
		x= ((x * $mul) >> $shift) + ((*name++ & 0xFF) << 4);
	
	return x & $mask;
}

const int IntConstListCount= ${\scalar @constlist};
const IntConstListEntry IntConstList[]= {
	{ NULL, 0 },
$const_entries
	{ NULL, 0 }
};
const int IntConstHashTableSize= $table_size;
const uint16_t IntConstHashTable[]= {
$hash_entries
};

const IntConstListEntry *GetIntConst(const char *Name) {
	int code= IntConstHashFunc(Name);
	int lim= code + $scan_dist + 1;
	/* scan forward at most $scan_dist table entries looking for the given Name.
	 * No need to wrap, because the table is longer than the hash function mask. */
	while (code < lim) {
		if (IntConstHashTable[code] && strcmp(IntConstList[IntConstHashTable[code]].Name, Name) == 0)
			return &IntConstList[IntConstHashTable[code]];
		code++;
	}
	return NULL;
}

END
