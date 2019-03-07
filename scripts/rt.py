#!/usr/bin/env python3

import subprocess
import argparse
import tempfile
import os


def run_cmd(cmd, timeout):
    print("Running: ", cmd)
    return subprocess.run(cmd, capture_output=True,
                          timeout=timeout, text=True)


def compile(clang, input, output, timeout, options=None):
    cmd = []
    cmd.append(clang)
    if options is not None:
        cmd.extend(options)
    cmd.extend([input, "-o", output])
    p = run_cmd(cmd, timeout)
    assert p.returncode == 0, \
        "Clang failure."
    assert len(p.stderr) == 0, \
        "errors or warnings during compilation: " + p.stderr

    return p


def decompile(rellic, input, output, timeout):
    cmd = []
    cmd.append(rellic)
    cmd.extend(["--input", input, "--output", output])
    p = run_cmd(cmd, timeout)
    assert p.returncode == 0, \
        "rellic-decomp failure."
    assert len(p.stderr) == 0, \
        "errors or warnings during decompilation: " + p.stderr

    return p


def roundtrip(rellic, filename, clang, timeout):
    with tempfile.TemporaryDirectory() as tempdir:
        print ("Testing: ", filename)
        out1 = os.path.join(tempdir, "out1")
        compile(clang, filename, out1, timeout)

        # capture binary run outputs
        cp1 = run_cmd(out1, timeout)

        rt_bc = os.path.join(tempdir, "rt.bc")
        compile(clang, filename, rt_bc, timeout, ["-c", "-emit-llvm"])

        rt_c = os.path.join(tempdir, "rt.c")
        decompile(rellic, rt_bc, rt_c, timeout)

        out2 = os.path.join(tempdir, "out2")
        compile(clang, rt_c, out2, timeout, ["-Wno-everything"])

        # capture outputs of binary after roundtrip
        cp2 = run_cmd(out2, timeout)

    assert cp1.stderr == cp2.stderr, \
        "Different stderr."
    assert cp1.stdout == cp2.stdout, \
        "Different stdout."
    assert cp1.returncode == cp2.returncode, \
        "Different return code."
    print("-------------------------------")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("test_path",
                        help="path to source code test file(s)")
    parser.add_argument("clang", help="path to clang")
    parser.add_argument(
        "-t",
        "--timeout",
        help="set timeout in seconds",
        type=int)
    args = parser.parse_args()
    if os.path.isdir(args.test_path):
        with os.scandir(args.test_path) as it:
            for item in it:
                roundtrip(args.rellic, item.path, args.clang, args.timeout)
    else:
        roundtrip(args.rellic, args.test_path, args.clang, args.timeout)


if __name__ == "__main__":
    main()
