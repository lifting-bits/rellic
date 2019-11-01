#!/usr/bin/env python

import unittest
import subprocess
import argparse
import tempfile
import os
import sys

class RunError(Exception):
    def __init__(self, msg):
        self.msg = msg

    def __str__(self):
        return str(self.msg)


def run_cmd(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True,
                           timeout=timeout, text=True)
    except FileNotFoundError as e:
        raise RunError(
            "Error: No such file or directory: \"" +
            e.filename +
            "\"")
    except PermissionError as e:
        raise RunError(
            "Error: File \"" +
            e.filename +
            "\" is not an executable.")

    return p


def compile(clang, input, output, timeout, options=None):
    cmd = []
    cmd.append(clang)
    if options is not None:
        cmd.extend(options)
    cmd.extend([input, "-o", output])
    p = run_cmd(cmd, timeout)
    assert p.returncode == 0, \
        "clang failure"
    assert len(p.stderr) == 0, \
        "errors or warnings during compilation: " + p.stderr
    return p


def decompile(rellic, input, output, timeout):
    cmd = []
    cmd.append(rellic)
    cmd.extend(["--input", input, "--output", output])
    p = run_cmd(cmd, timeout)
    assert p.returncode == 0, \
        "rellic-decomp failure: " + p.stderr
    assert len(p.stderr) == 0, \
        "errors or warnings during decompilation: " + p.stderr
    return p


def roundtrip(rellic, filename, clang, timeout):
    with tempfile.TemporaryDirectory() as tempdir:
        out1 = os.path.join(tempdir, "out1")
        compile(clang, filename, out1, timeout)

        # capture binary run outputs
        cp1 = run_cmd([out1], timeout)

        rt_bc = os.path.join(tempdir, "rt.bc")
        compile(clang, filename, rt_bc, timeout, ["-c", "-emit-llvm"])

        rt_c = os.path.join(tempdir, "rt.c")
        decompile(rellic, rt_bc, rt_c, timeout)

        out2 = os.path.join(tempdir, "out2")
        compile(clang, rt_c, out2, timeout, ["-Wno-everything"])

        # capture outputs of binary after roundtrip
        cp2 = run_cmd([out2], timeout)

        assert cp1.stderr == cp2.stderr, \
            "Different stderr"
        assert cp1.stdout == cp2.stdout, \
            "Different stdout"
        assert cp1.returncode == cp2.returncode, \
            "Different return code"


class TestRoundtrip(unittest.TestCase):
    pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("tests",
                        help="path to test file or directory")
    parser.add_argument("clang", help="path to clang")
    parser.add_argument(
        "-t",
        "--timeout",
        help="set timeout in seconds",
        type=int)
    
    args = parser.parse_args()

    def test_generator(path):
        def test(self):
            roundtrip(args.rellic, path, args.clang, args.timeout)
        return test
    
    with os.scandir(args.tests) as it:
        for item in it:
            test_name = 'test_%s' % os.path.splitext(item.name)[0]
            test = test_generator(item.path)
            setattr(TestRoundtrip, test_name, test)
    
    unittest.main(argv=[sys.argv[0]])
