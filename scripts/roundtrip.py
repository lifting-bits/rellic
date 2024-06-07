#!/usr/bin/env python3

import unittest
import subprocess
import argparse
import tempfile
import os
import sys
from pathlib import Path


class RunError(Exception):
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return str(self.msg)


def run_cmd(cmd, timeout):
    try:
        p = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            universal_newlines=True,
        )
    except FileNotFoundError as e:
        raise RunError('Error: No such file or directory: "' + e.filename + '"')
    except PermissionError as e:
        raise RunError('Error: File "' + e.filename + '" is not an executable.')

    return p


def compile(self, clang, input, output, timeout, options=None):
    cmd = []
    cmd.append(clang)
    if options is not None:
        cmd.extend(options)
    cmd.extend([input, "-o", output])
    p = run_cmd(cmd, timeout)

    self.assertEqual(
        len(p.stderr), 0, "errors or warnings during compilation: %s" % p.stderr
    )
    self.assertEqual(p.returncode, 0, "clang failure")

    return p


def decompile(self, rellic, input, output, timeout):
    cmd = [rellic]
    cmd.extend(
        ["--input", input, "--output", output]
    )
    p = run_cmd(cmd, timeout)

    self.assertEqual(p.returncode, 0, "rellic-decomp failure: %s" % p.stderr)
    self.assertEqual(
        len(p.stderr), 0, "errors or warnings during decompilation: %s" % p.stderr
    )

    return p


def roundtrip(self, rellic, filename, clang, timeout, translate_only, general_flags, binary_compile_flags, bitcode_compile_flags, recompile_flags):
    with tempfile.TemporaryDirectory() as tempdir:
        out1 = os.path.join(tempdir, "out1")
        compile(self, clang, filename, out1, timeout, general_flags + binary_compile_flags)

        # capture binary run outputs
        cp1 = run_cmd([out1], timeout)

        rt_bc = os.path.join(tempdir, "rt.bc")
        flags = ["-c", "-emit-llvm"]
        compile(self, clang, filename, rt_bc, timeout, general_flags + bitcode_compile_flags + flags)

        rt_c = os.path.join(tempdir, "rt.c")
        decompile(self, rellic, rt_bc, rt_c, timeout)

        # ensure there is a C output file
        self.assertTrue(os.path.exists(rt_c))

        # ensure the file has some C
        self.assertTrue(os.path.getsize(rt_c) > 0)

        # We should recompile, lets see how this goes
        if not translate_only:
            out2 = os.path.join(tempdir, "out2")
            compile(self, clang, rt_c, out2, timeout, general_flags + recompile_flags + ["-Wno-everything"])

            # capture outputs of binary after roundtrip
            cp2 = run_cmd([out2], timeout)

            self.assertEqual(cp1.stderr, cp2.stderr, "Different stderr")
            self.assertEqual(cp1.stdout, cp2.stdout, "Different stdout")
            self.assertEqual(cp1.returncode, cp2.returncode, "Different return code")


class TestRoundtrip(unittest.TestCase):
    pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("tests", help="path to test directory")
    parser.add_argument("clang", help="path to clang")
    parser.add_argument(
        "--translate-only", action="store_true", default=False, help="Translate only, do not recompile"
    )
    parser.add_argument(
        "--recurse-subdirs", action="store_true", default=False, help="Recurse into subdirectories when looking for source files"
    )
    parser.add_argument("-t", "--timeout", help="set timeout in seconds", type=int)
    parser.add_argument(
        "--cflags", help="additional CFLAGS", action='append', default=[], type=str)

    args = parser.parse_args()

    def test_generator(path):
        def test(self):
            roundtrip(self, args.rellic, path, args.clang, args.timeout, args.translate_only, args.cflags, [], []     , [])
            roundtrip(self, args.rellic, path, args.clang, args.timeout, args.translate_only, args.cflags, [], ["-O1"], [])
            roundtrip(self, args.rellic, path, args.clang, args.timeout, args.translate_only, args.cflags, [], ["-O2"], [])
            roundtrip(self, args.rellic, path, args.clang, args.timeout, args.translate_only, args.cflags, [], ["-O3"], [])
            roundtrip(self, args.rellic, path, args.clang, args.timeout, args.translate_only, args.cflags, [], ["-g3"], [])

        return test

    tests_path = Path(args.tests)
    glob_pattern = "**/*" if args.recurse_subdirs else "*"
    for item in tests_path.glob(glob_pattern):
        # Allow for READMEs and data/headers
        if item.is_file() and item.suffix in [".c", ".cpp"]:
            # in case we recurse into subdirs, there might be duplicate
            # filenames
            if args.recurse_subdirs:
                # get the path relative to the test directory
                item_rel_to_tests = item.relative_to(tests_path)
                test_name = f"test_{'-'.join(item.parts[:-1])}_{item.name}"
            else:
                test_name = f"test_{item.name}"
            test = test_generator(str(item))
            setattr(TestRoundtrip, test_name, test)

    unittest.main(argv=[sys.argv[0]])
