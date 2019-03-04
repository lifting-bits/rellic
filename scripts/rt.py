#!/usr/bin/env python3

import subprocess
import os
import argparse


def roundtrip(rellic, filename, clang, set_timeout):
    subprocess.run([clang, filename,
                    "-o", "output1"], timeout=set_timeout)
    compl_process1 = subprocess.run(
        "./output1", capture_output=True, timeout=set_timeout, text=True)
    subprocess.run([clang, "-c",
                    "-emit-llvm", filename, "-o", "roundtrip.bc"], 
                    timeout=set_timeout)
    subprocess.run([rellic, "--input",
                    "roundtrip.bc", "--output", "roundtrip.c"], timeout=set_timeout)
    subprocess.run([clang,
                    "-Wno-everything", "roundtrip.c",
                    "-o", "output2"], timeout=set_timeout)
    compl_process2 = subprocess.run(
        "./output2", capture_output=True, timeout=set_timeout, text=True)
    os.remove("roundtrip.bc")
    os.remove("roundtrip.c")
    os.remove("output1")
    os.remove("output2")
    return compl_process1, compl_process2


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
