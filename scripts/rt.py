#!/usr/bin/env python3

import subprocess
import argparse
import tempfile


def run_cmd(cmd, timeout):
    return subprocess.run(cmd, capture_output=True,
                          timeout=timeout, text=True)


def roundtrip(rellic, filename, clang, timeout):
    with tempfile.TemporaryDirectory() as tempdir:
        run_cmd([clang, filename, "-o", tempdir + "/out1"], timeout)     
        cproc1 = run_cmd(tempdir + "/out1", timeout)
        run_cmd([clang, "-c", "-emit-llvm", filename,
                       "-o", tempdir + "/roundtrip.bc"], timeout)
        run_cmd([rellic, "--input", tempdir + "/roundtrip.bc",
                         "--output", tempdir + "/roundtrip.c"], timeout)
        run_cmd([clang, tempdir + "/roundtrip.c",
                               "-o", tempdir + "/out2"], timeout)
        cproc2 = run_cmd(tempdir + "/out2", timeout)

    assert cproc1.stderr == cproc2.stderr, \
        "Different stderr."
    assert cproc1.stdout == cproc2.stdout, \
        "Different stdout."
    assert cproc1.returncode == cproc2.returncode, \
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
