#!/usr/bin/env python3

import subprocess
import argparse
import tempfile
import os.path


def run_cmd(cmd, timeout):
    return subprocess.run(cmd, capture_output=True,
                          timeout=timeout, text=True)


def compile(input, output, args):
    pass


def decompile(input, output, args):
    pass


def roundtrip(rellic, filename, clang, timeout):
    with tempfile.TemporaryDirectory() as tempdir:
        out1_path = os.path.join(tempdir, "out1")
        rt_bc_path = os.path.join(tempdir, "rt.bc")
        rt_c_path = os.path.join(tempdir, "rt.c")
        out2_path = os.path.join(tempdir, "out2")

        p = run_cmd([clang, filename, "-o", out1_path], timeout)
        assert p.returncode == 0, "Clang failure."
        assert len(p.stderr) == 0, "errors or warnings during compilation: " + p.stderr
        cp1 = run_cmd(out1_path, timeout)
        p = run_cmd([clang, "-c", "-emit-llvm", filename,
                 "-o", rt_bc_path], timeout)
        assert p.returncode == 0, "Clang failure."
        assert len(p.stderr) == 0, "errors or warnings during compilation: " + p.stderr
        p = run_cmd([rellic, "--input", rt_bc_path,
                         "--output", rt_c_path], timeout)
        assert p.returncode == 0, "rellic-decomp failure."
        assert len(p.stderr) == 0, "errors or warnings during decompilation: " + p.stderr
        p = run_cmd([clang, rt_c_path,
                 "-o", out2_path], timeout)
        assert p.returncode == 0, "Clang failure."
#       assert len(p.stderr) == 0, "errors or warnings during compilation: " + p.stderr
        cp2 = run_cmd(out2_path, timeout)

    assert cp1.stderr == cp2.stderr, \
        "Different stderr."
    assert cp1.stdout == cp2.stdout, \
        "Different stdout."
    assert cp1.returncode == cp2.returncode, \
        "Different return code."


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("filename",
                        help="path to source code test file")
    parser.add_argument("clang", help="path to clang")
    parser.add_argument(
        "-t",
        "--timeout",
        help="set timeout in seconds",
        type=int)
    args = parser.parse_args()
    roundtrip(args.rellic, args.filename, args.clang, args.timeout)


if __name__ == "__main__":
    main()
