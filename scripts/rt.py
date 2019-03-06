#!/usr/bin/env python3

import subprocess
import argparse
import tempfile
import os.path


def run_cmd(cmd, timeout):
    return subprocess.run(cmd, capture_output=True,
                          timeout=timeout, text=True)


def compile(clang, input, output, timeout, options=None):
    cmd = []
    cmd.append(clang)
    if options is not None:
        cmd.extend(options)
    cmd.extend([input, "-o", output])

    return run_cmd(cmd, timeout)


def decompile(rellic, input, output, timeout):
    cmd = []
    cmd.append(rellic)
    cmd.extend(["--input", input, "--output", output])

    return run_cmd(cmd, timeout)


def roundtrip(rellic, filename, clang, timeout):
    with tempfile.TemporaryDirectory() as tempdir:
        # test compilation to executable
        out1 = os.path.join(tempdir, "out1")
        p = compile(clang, filename, out1, timeout)
        assert p.returncode == 0, \
            "Clang failure."
        assert len(p.stderr) == 0, \
            "errors or warnings during compilation: " + p.stderr

        # capture binary run outputs
        cp1 = run_cmd(out1, timeout)

        # test compilation to bitcode
        rt_bc = os.path.join(tempdir, "rt.bc")
        p = compile(clang, filename, rt_bc, timeout, ["-c", "-emit-llvm"])
        assert p.returncode == 0, \
            "Clang failure."
        assert len(p.stderr) == 0, \
            "errors or warnings during compilation: " + p.stderr

        # test decompilation from bitcode to C code
        rt_c = os.path.join(tempdir, "rt.c")
        p = decompile(rellic, rt_bc, rt_c, timeout)
        assert p.returncode == 0, \
            "rellic-decomp failure."
        assert len(p.stderr) == 0, \
            "errors or warnings during decompilation: " + p.stderr

        # test compilation to executable
        out2 = os.path.join(tempdir, "out2")
        p = compile(clang, rt_c, out2, timeout)
        assert p.returncode == 0, "Clang failure."
#       assert len(p.stderr) == 0, \
#           "errors or warnings during compilation: " + p.stderr

        # capture outputs of binary after roundtrip
        cp2 = run_cmd(out2, timeout)

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
