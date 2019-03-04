#!/usr/bin/env python3

import subprocess
import os
import argparse


def run_cmd(arg_list, set_timeout):
    return subprocess.run(arg_list, capture_output=True,
                          timeout=set_timeout, text=True)


def roundtrip(rellic, filename, clang, set_timeout):
    run_cmd([clang, filename, "-o", "output1"], set_timeout)
    cproc1 = run_cmd("./output1", set_timeout)
    run_cmd([clang, "-c", "-emit-llvm", filename,
             "-o", "roundtrip.bc"], set_timeout)
    run_cmd([rellic, "--input", "roundtrip.bc",
             "--output", "roundtrip.c"], set_timeout)
    run_cmd([clang,
             "-Wno-everything", "roundtrip.c",
             "-o", "output2"], set_timeout)
    cproc2 = run_cmd("./output2", set_timeout)
    os.remove("roundtrip.bc")
    os.remove("roundtrip.c")
    os.remove("output1")
    os.remove("output2")
    return cproc1, cproc2


def compare(two_things):
    assert two_things[0].stderr == two_things[1].stderr, \
        "Different stderr."
    assert two_things[0].stdout == two_things[1].stdout, \
        "Different stdout."
    assert two_things[0].returncode == two_things[1].returncode, \
        "Different return code."


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rellic", help="path to rellic-decomp")
    parser.add_argument("filename",
                        help="path to source code test file")
    parser.add_argument("clang", help="path to clang")
    parser.add_argument("--timeout", help="set timeout in seconds", type=int)
    args = parser.parse_args()
    outputs = roundtrip(args.rellic, args.filename, args.clang, args.timeout)
    compare(outputs)


if __name__ == "__main__":
    main()
