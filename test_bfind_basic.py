#!/usr/bin/env python3
"""
test_bfind_basic.py - Basic sanity tests for the bfind assignment.

This is a small test harness to help you check your progress as you
implement bfind. It does NOT cover every case — passing all of these
does not guarantee full marks. Use this alongside manual testing.

Usage:
    python3 test_bfind_basic.py [--verbose]
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
BOLD = "\033[1m"
RESET = "\033[0m"

VERBOSE = False
BFIND = "./bfind"
passed = 0
failed = 0


def run_bfind(args, cwd=None, timeout=10):
    """Run bfind and return (returncode, stdout lines, stderr)."""
    cmd = [os.path.abspath(BFIND)] if cwd else [BFIND]
    cmd += args
    if VERBOSE:
        print(f"  {YELLOW}${RESET} {' '.join(cmd)}")
    try:
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=timeout, cwd=cwd,
        )
        lines = [l for l in proc.stdout.strip().split("\n") if l]
        if VERBOSE and lines:
            for l in lines[:20]:
                print(f"    {l}")
            if len(lines) > 20:
                print(f"    ... ({len(lines) - 20} more)")
        return proc.returncode, lines, proc.stderr
    except subprocess.TimeoutExpired:
        return -1, [], "TIMEOUT"


def check(name, condition, detail=""):
    """Record a test result."""
    global passed, failed
    if condition:
        passed += 1
        print(f"{GREEN}[PASS]{RESET} {name}")
    else:
        failed += 1
        print(f"{RED}[FAIL]{RESET} {name}")
        if detail:
            print(f"       {detail}")


# ------------------------------------------------------------------ #
#  Tests                                                               #
# ------------------------------------------------------------------ #

def test_traversal(tmpdir):
    """Tests for BFS traversal (TODO 4, the core function)."""
    print(f"\n{BOLD}--- BFS Traversal ---{RESET}")

    # -- Flat directory (no recursion) --
    flat = os.path.join(tmpdir, "flat")
    os.makedirs(flat)
    for name in ["a.txt", "b.txt", "c.txt"]:
        open(os.path.join(flat, name), "w").close()

    rc, lines, _ = run_bfind([flat])
    check("Lists all entries in a flat directory",
          rc == 0 and set(lines) == {flat, f"{flat}/a.txt",
                                     f"{flat}/b.txt", f"{flat}/c.txt"},
          f"Got: {set(lines)}")

    # -- Recursive traversal with BFS ordering --
    tree = os.path.join(tmpdir, "tree")
    os.makedirs(f"{tree}/d1/d2")
    open(f"{tree}/top.txt", "w").close()
    open(f"{tree}/d1/mid.txt", "w").close()
    open(f"{tree}/d1/d2/deep.txt", "w").close()

    rc, lines, _ = run_bfind([tree])
    expected = {tree, f"{tree}/top.txt", f"{tree}/d1",
                f"{tree}/d1/mid.txt", f"{tree}/d1/d2",
                f"{tree}/d1/d2/deep.txt"}
    check("Finds all entries recursively",
          rc == 0 and set(lines) == expected,
          f"Expected: {expected}\n       Got:      {set(lines)}")

    # BFS order: depth must be non-decreasing
    if lines:
        base_depth = tree.rstrip("/").count("/")
        depths = [l.rstrip("/").count("/") - base_depth for l in lines]
        check("Output is in BFS order (depths non-decreasing)",
              all(depths[i] <= depths[i+1] for i in range(len(depths)-1)),
              f"Depths: {depths}")
    else:
        check("Output is in BFS order (depths non-decreasing)", False,
              "No output to check")

    # -- Default path '.' --
    simple = os.path.join(tmpdir, "dottest")
    os.makedirs(simple)
    open(f"{simple}/f.txt", "w").close()

    rc, lines, _ = run_bfind([], cwd=simple)
    check("Defaults to '.' when no path given",
          rc == 0 and "." in set(lines) and "./f.txt" in set(lines),
          f"Got: {set(lines)}")


def test_filters(tmpdir):
    """Tests for filter matching (TODOs 1-3)."""
    print(f"\n{BOLD}--- Filters ---{RESET}")

    tree = os.path.join(tmpdir, "filt")
    os.makedirs(f"{tree}/sub")
    # Create files with known properties
    with open(f"{tree}/hello.c", "wb") as f:
        f.write(b"x" * 200)
    with open(f"{tree}/readme.txt", "wb") as f:
        f.write(b"x" * 50)
    with open(f"{tree}/sub/main.c", "wb") as f:
        f.write(b"x" * 3000)
    open(f"{tree}/empty.c", "w").close()

    # -name
    rc, lines, _ = run_bfind([tree, "-name", "*.c"])
    found = set(lines)
    check("-name '*.c' matches .c files",
          rc == 0 and f"{tree}/hello.c" in found
          and f"{tree}/sub/main.c" in found
          and f"{tree}/empty.c" in found
          and f"{tree}/readme.txt" not in found,
          f"Got: {found}")

    # -type f
    rc, lines, _ = run_bfind([tree, "-type", "f"])
    found = set(lines)
    check("-type f excludes directories",
          rc == 0 and tree not in found and f"{tree}/sub" not in found
          and f"{tree}/hello.c" in found,
          f"Got: {found}")

    # -type d
    rc, lines, _ = run_bfind([tree, "-type", "d"])
    found = set(lines)
    check("-type d returns only directories",
          rc == 0 and found == {tree, f"{tree}/sub"},
          f"Got: {found}")

    # -size
    rc, lines, _ = run_bfind([tree, "-size", "+1k", "-type", "f"])
    found = set(lines)
    check("-size +1k finds files over 1024 bytes",
          rc == 0 and found == {f"{tree}/sub/main.c"},
          f"Got: {found}")

    # -size exact
    rc, lines, _ = run_bfind([tree, "-size", "0c", "-type", "f"])
    found = set(lines)
    check("-size 0c finds empty files",
          rc == 0 and found == {f"{tree}/empty.c"},
          f"Got: {found}")

    # Combined filters (AND)
    rc, lines, _ = run_bfind([tree, "-name", "*.c", "-size", "+100c"])
    found = set(lines)
    check("-name '*.c' -size +100c (AND semantics)",
          rc == 0 and found == {f"{tree}/hello.c", f"{tree}/sub/main.c"},
          f"Got: {found}")

    # No matches
    rc, lines, _ = run_bfind([tree, "-name", "*.xyz"])
    check("No matches produces empty output",
          rc == 0 and len(lines) == 0,
          f"Got {len(lines)} lines")


def test_symlinks(tmpdir):
    """Tests for symlink handling and cycle detection (part of TODO 4)."""
    print(f"\n{BOLD}--- Symlinks ---{RESET}")

    tree = os.path.join(tmpdir, "sym")
    os.makedirs(f"{tree}/real")
    open(f"{tree}/real/file.txt", "w").close()
    os.symlink("real", f"{tree}/link")

    # Without -L: symlink listed but not descended
    rc, lines, _ = run_bfind([tree])
    found = set(lines)
    has_link = f"{tree}/link" in found
    not_followed = not any(l.startswith(f"{tree}/link/") for l in lines)
    check("Without -L: symlink listed but not followed",
          rc == 0 and has_link and not_followed,
          f"link listed: {has_link}, not descended: {not_followed}")

    # With -L: symlink is followed AND real directory still works
    rc, lines, _ = run_bfind(["-L", tree])
    found = set(lines)
    link_followed = f"{tree}/link/file.txt" in found
    real_works = f"{tree}/real/file.txt" in found
    check("With -L: symlink followed AND real dir still descended",
          rc == 0 and link_followed and real_works,
          f"link children: {link_followed}, real children: {real_works}")

    # Cycle detection
    cycle = os.path.join(tmpdir, "cyc")
    os.makedirs(f"{cycle}/a/b")
    open(f"{cycle}/a/file.txt", "w").close()
    os.symlink(os.path.join(cycle, "a"), f"{cycle}/a/b/loop")

    rc, lines, stderr = run_bfind(["-L", cycle], timeout=5)
    check("With -L: cycle detection prevents infinite loop",
          rc != -1 and rc == 0 and len(lines) > 0,
          "TIMEOUT — bfind ran forever" if rc == -1
          else f"rc={rc}, {len(lines)} entries")


def test_errors(tmpdir):
    """Tests for error handling."""
    print(f"\n{BOLD}--- Error Handling ---{RESET}")

    # Nonexistent path
    rc, lines, stderr = run_bfind(["/nonexistent/path/12345"])
    check("Nonexistent path: prints error, doesn't crash",
          len(stderr) > 0 or rc != 0,
          f"rc={rc}, stderr={'(present)' if stderr else '(empty)'}")


# ------------------------------------------------------------------ #
#  Main                                                                #
# ------------------------------------------------------------------ #

def main():
    global VERBOSE, BFIND

    parser = argparse.ArgumentParser(
        description="Basic sanity tests for bfind")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--bfind", default="./bfind")
    args = parser.parse_args()
    VERBOSE = args.verbose
    BFIND = args.bfind

    if not os.path.isfile(BFIND):
        print(f"{RED}Error:{RESET} '{BFIND}' not found. Run 'make' first.")
        return 1

    print(f"\n{BOLD}=== bfind Basic Tests ==={RESET}")

    with tempfile.TemporaryDirectory() as tmpdir:
        test_traversal(tmpdir)
        test_filters(tmpdir)
        test_symlinks(tmpdir)
        test_errors(tmpdir)

    total = passed + failed
    print(f"\n{BOLD}=== Results ==={RESET}")
    print(f"  {GREEN}Passed: {passed}/{total}{RESET}")
    if failed:
        print(f"  {RED}Failed: {failed}/{total}{RESET}")
    else:
        print(f"  {GREEN}All tests passed!{RESET}")
    print(f"\n  Note: This is not the full test suite. Passing all of")
    print(f"  these does not guarantee full marks. Test edge cases")
    print(f"  manually and compare against the system 'find' command.\n")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
