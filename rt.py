import subprocess
import sys


subprocess.run(["rellic-build/libraries/llvm/bin/clang", sys.argv[1]])

compl_process1 = subprocess.run("./a.out", capture_output=True, text=True)

subprocess.run(["rm", "a.out"])


subprocess.run(["rellic-build/libraries/llvm/bin/clang", "-c", "-emit-llvm", sys.argv[1], "-o", "roundtrip.bc"])

subprocess.run(["rellic-build/rellic-decomp-4.0", "--input", "roundtrip.bc", "--output", "roundtrip.c"])

subprocess.run(["rellic-build/libraries/llvm/bin/clang", "-Wno-everything", "roundtrip.c"])

compl_process2 = subprocess.run("./a.out", capture_output=True, text=True)

subprocess.run(["rm", "roundtrip.bc", "roundtrip.c", "a.out"])


if compl_process1.stderr == compl_process2.stderr and compl_process1.stdout == compl_process2.stdout and compl_process1.returncode == compl_process2.returncode:
	print("Same outputs.")
else:
	print("Different outputs.")


