import subprocess
import os
import argparse

def execute_cmd(filename):
	subprocess.run(["rellic-build/libraries/llvm/bin/clang", filename,
			"-o", "output1"])
	compl_process1 = subprocess.run("./output1", capture_output=True, text=True)
	subprocess.run(["rellic-build/libraries/llvm/bin/clang", "-c",
	                "-emit-llvm", filename, "-o", "roundtrip.bc"])
	subprocess.run(["rellic-build/rellic-decomp-4.0", "--input",
	                "roundtrip.bc", "--output", "roundtrip.c"])
	subprocess.run(["rellic-build/libraries/llvm/bin/clang",
	                "-Wno-everything", "roundtrip.c",
			 "-o", "output2"])
	compl_process2 = subprocess.run("./output2", capture_output=True, text=True)
	os.remove("roundtrip.bc")
	os.remove("roundtrip.c")
	os.remove("output1")
	os.remove("output2")

	return compl_process1, compl_process2


def compare(two_things):
	assert two_things[0].stderr == two_things[1].stderr, \
		"Different stderr."
	assert two_things[0].stdout == two_things[1].stdout,  \
		"Different stdout."
	assert two_things[0].returncode == two_things[1].returncode, \
		"Different return code."

	print ("Same outputs.")









def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("filename",
                            help="name of the source code file or path to it")

	args = parser.parse_args()
	outputs = execute_cmd(args.filename)
	compare(outputs)


main()
