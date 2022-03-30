#!/usr/bin/env python3

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


def decompile(self, rellic, input, output, timeout):
    cmd = [rellic]
    cmd.extend(
        ["--lower_switch", "--input", input, "--output", output]
    )
    p = run_cmd(cmd, timeout)

    self.assertEqual(p.returncode, 0, "rellic-decomp failure: %s" % p.stderr)
    self.assertEqual(
        len(p.stderr), 0, "errors or warnings during decompilation: %s" % p.stderr
    )

    return p

class TestDecompile(unittest.TestCase):
    pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("tests", help="path to test directory")
    parser.add_argument("-t", "--timeout", help="set timeout in seconds", type=int)

    args = parser.parse_args()

    def test_generator(path):
        def test(self):
            with tempfile.TemporaryDirectory() as tempdir:
                rt_c = os.path.join(tempdir, "rt.c")
                decompile(self, args.rellic, path, rt_c, args.timeout)

        return test

    for item in os.scandir(args.tests):
        if item.is_file():
            name, ext = os.path.splitext(item.name)
            # Allow for READMEs and data/headers
            if ext in [".bc ", ".ll"]:
                test_name = f"test_{name}"
                test = test_generator(item.path)
                setattr(TestDecompile, test_name, test)

    unittest.main(argv=[sys.argv[0]])
