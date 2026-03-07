#!/bin/bash
#
# create_test_tree.sh - Create a known directory tree for testing bfind.
#
# Usage: ./create_test_tree.sh [directory]
#   Creates the test tree under the given directory (default: test_tree).
#

set -e

DIR="${1:-test_tree}"

# Clean up any previous test tree
rm -rf "$DIR"

# ---- Structure ----
#
# test_tree/
#   alpha/
#     one.c          (100 bytes, 0644)
#     two.txt        (50 bytes,  0644)
#     beta/
#       three.c      (2000 bytes, 0755)
#       four.h       (10 bytes,   0600)
#   gamma/
#     five.c         (0 bytes, 0644)
#     six.log        (5000 bytes, 0644)
#     delta/
#       seven.txt    (300 bytes, 0444)
#   epsilon/
#     (empty directory)
#   link_to_alpha -> alpha           (symlink to directory)
#   link_to_one.c -> alpha/one.c    (symlink to file)

mkdir -p "$DIR/alpha/beta"
mkdir -p "$DIR/gamma/delta"
mkdir -p "$DIR/epsilon"

# Create files with specific sizes
dd if=/dev/zero of="$DIR/alpha/one.c"       bs=1 count=100  2>/dev/null
dd if=/dev/zero of="$DIR/alpha/two.txt"     bs=1 count=50   2>/dev/null
dd if=/dev/zero of="$DIR/alpha/beta/three.c" bs=1 count=2000 2>/dev/null
dd if=/dev/zero of="$DIR/alpha/beta/four.h"  bs=1 count=10   2>/dev/null
dd if=/dev/zero of="$DIR/gamma/five.c"       bs=1 count=0    2>/dev/null
dd if=/dev/zero of="$DIR/gamma/six.log"      bs=1 count=5000 2>/dev/null
dd if=/dev/zero of="$DIR/gamma/delta/seven.txt" bs=1 count=300 2>/dev/null

# Set permissions
chmod 0644 "$DIR/alpha/one.c"
chmod 0644 "$DIR/alpha/two.txt"
chmod 0755 "$DIR/alpha/beta/three.c"
chmod 0600 "$DIR/alpha/beta/four.h"
chmod 0644 "$DIR/gamma/five.c"
chmod 0644 "$DIR/gamma/six.log"
chmod 0444 "$DIR/gamma/delta/seven.txt"

# Create symlinks
ln -s alpha "$DIR/link_to_alpha"
ln -s alpha/one.c "$DIR/link_to_one.c"

# Create a cycle for -L testing
ln -s ../.. "$DIR/gamma/delta/cycle_link"

echo "Test tree created in $DIR/"
