#!/usr/bin/env python3

from pathlib import Path
from os import path
import argparse
import sys
import shutil

MYDIR = path.dirname(path.abspath(__file__))
ARCHMAP = {
    "amd64": ["--target=x86_64-pc-linux-gnu"],
    "arm64": ["--target=aarch64-linux-gnu", "-isystem/usr/aarch64-linux-gnu/include"],
    "x86": ["--target=i686-linux-gnu"],
    "armv7": ["--target=arm-linux-gnueabihf", "-march=armv7a", "-isystem/usr/arm-linux-gnueabihf/include"],
}

EXTRA_OPTIONS = [
    "-O0",  # code for many tests will get optimized out if we enable optimizations
    "-ggdb",  # emit debug info
    "-pipe",  # use less filesystem
    "-w",  # ignore warnings

    # extra CFLAGS specified in CMakeLists.txt
    "-Wno-implicit-int",
    "-Wno-int-conversion",
    "-Wno-implicit-function-declaration",
]

ignored_files_dir_ieee = """
# Tests with features unsupported by Clang (usually GCC extensions)
# (Big list of naughty tests)
# file(GLOB UnsupportedTests
#  CONFIGURE_DEPENDS

  # The following all expect very specific optimiser behavior from the compiler

  # Clang at O0 does not work out the code referencing the undefined symbol can
  # never be executed
  fp-cmp-7.c
# )
# list(APPEND TestsToSkip ${UnsupportedTests})

##
## Tests that require extra CFLAGS in Clang
##

# Tests that require libm (-lm flag)
# file(GLOB TestRequiresLibM CONFIGURE_DEPENDS
  20041213-1.c
  mzero4.c
# )

# Tests that require -fno-trapping-math
# file(GLOB TestRequiresFNoTrappingMath CONFIGURE_DEPENDS
  # Needs additional flags from compare-fp-3.x
  compare-fp-3.c
# )
"""

ignored_files_dir_execute = """
# GCC Extension: Nested functions
20000822-1.c
20010209-1.c
20010605-1.c
20030501-1.c
20040520-1.c
20061220-1.c
20090219-1.c
920415-1.c
920428-2.c
920501-7.c
920612-2.c
920721-4.c
921017-1.c
921215-1.c
931002-1.c
comp-goto-2.c
nest-align-1.c
nest-stdar-1.c
nestfunc-1.c
nestfunc-2.c
nestfunc-3.c
nestfunc-5.c
nestfunc-6.c
nestfunc-7.c
pr22061-3.c
pr22061-4.c
pr24135.c
pr51447.c
pr71494.c

# Variable length arrays in structs
20020412-1.c
20040308-1.c
20040423-1.c
20041218-2.c
20070919-1.c
align-nest.c
pr41935.c
pr82210.c

# Initialization of flexible array member
pr28865.c

# Initialization of union is not required to initialize padding.
pr19687.c

# GCC Extension: __builtin_*
pr39228.c          # __builtin_isinff, __builtin_isinfl
pr47237.c          # __builtin_apply, __builtin_apply_args
pr85331.c          # __builtin_shuffle
va-arg-pack-1.c    # __builtin_va_arg_pack

# Clang does not support 'DD' suffix on floating constant
pr80692.c

# Test requires compiler to recognise llabs without including <inttypes.h> -
# clang will only recognise this function if the header is included.
20021127-1.c

# Tests __attribute__((noinit))
noinit-attribute.c

# We are unable to parse the dg-additional-options for this test, which is
# required for it to work (we discard any with `target`, but we need the
# define for this test)
20101011-1.c

# The following rely on C Undefined Behavior

# Test relies on UB around (float)INT_MAX
20031003-1.c

# UB: Expects very specific behavior around setjmp/longjmp and allocas, which
# clang is not obliged to replicate.
pr64242.c

# UB: Creates two `restrict` pointers that alias in the same scope.
pr38212.c

# UB: Each comparisons in (cyx != cyy || mpn_cmp (dx, dy, size + 1) != 0 ||
#     dx[size] != 0x12345678) is UB on its own.
921202-1.c

# The following all expect very specific optimiser behavior from the compiler

# __builtin_return_address(n) with n > 0 not guaranteed to give expected result
20010122-1.c

# Expects gnu89 inline behavior
20001121-1.c
20020107-1.c
930526-1.c
961223-1.c
980608-1.c
bcp-1.c
loop-2c.c
p18298.c
restrict-1.c
unroll-1.c
va-arg-7.c
va-arg-8.c

# Clang at O0 does not work out the code referencing the undefined symbol can
# never be executed
medce-1.c

# Expects that function is always inlined
990208-1.c

# pragma optimize("-option") is ignored by Clang
alias-1.c
pr79043.c

# The following all expect very specific optimiser behavior from the compiler
# around __printf_chk and friends.
fprintf-chk-1.c
printf-chk-1.c
vfprintf-chk-1.c
vprintf-chk-1.c

# Clang at -O0 does not enable -finstrument-functions
# (https://bugs.llvm.org/show_bug.cgi?id=49143)
eeprof-1.c

# Size of array element is not a multiple of its alignment.
pr36093.c
pr43783.c

# Tests where clang currently has bugs or issues
# file(GLOB FailingTests CONFIGURE_DEPENDS

# Handling of bitfields is different between clang and GCC:
# http://lists.llvm.org/pipermail/llvm-dev/2017-October/118507.html
# https://gcc.gnu.org/ml/gcc/2017-10/msg00192.html
# http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1260.htm
bitfld-3.c
bitfld-5.c
pr32244-1.c
pr34971.c

# This causes a stacktrace on x86 in X86TargetLowering::LowerCallTo
pr84169.c

# clang complains the array is too large
991014-1.c

# __builtin_setjmp/__builtin_longjmp are interacting badly with optimisation
pr60003.c

# Tests that require -fwrapv
# file(GLOB TestRequiresFWrapV CONFIGURE_DEPENDS
# Test relies on undefined signed overflow behavior (int foo - INT_MIN).
20040409-1.c
20040409-2.c
20040409-3.c

# Tests that require -Wno-return-type
# file(GLOB TestRequiresWNoReturnType CONFIGURE_DEPENDS
# Non-void function must return a value
920302-1.c
920501-3.c
920728-1.c

# Tests that require libm (-lm ldflag)
# file(GLOB TestRequiresLibM CONFIGURE_DEPENDS
980709-1.c
float-floor.c
complex-5.c  # -lm needed on FreeBSD

# x86-only Tests
990413-2.c

# AArch64 Test Blacklist
# if(ARCH MATCHES "AArch64")
# file(GLOB AArch64TestsToSkip CONFIGURE_DEPENDS
# error: __builtin_longjmp is not supported for the current target
# error: __builtin_setjmp is not supported for the current target
built-in-setjmp.c
pr84521.c
# Triggers an assertion failure at at least -O3
va-arg-22.c
# Triggers an assertion failure in RegBankSelect pass at -O0 (GlobalIsel)
pr71626-1.c
pr71626-2.c


# ARM Test Blacklist
# if(ARCH MATCHES "ARM")
# file(GLOB ARMTestsToSkip CONFIGURE_DEPENDS
# No support for __int128 on ARM 32-bit
pr84748.c

# With some command line options, linking fails with:
# pr89434.c:(.text+0xac): undefined reference to `__mulodi4'
pr89434.c

# Fails in the front end with:
# clang: llvm/include/llvm/Support/Casting.h:269: typename
#   cast_retty<X, Y *>::ret_type llvm::cast(Y *) [X = llvm::PointerType,
#   Y = llvm::Type]: Assertion `isa<X>(Val) && "cast<Ty>() argument of
#   incompatible type!"' failed.
va-arg-22.c

# files that require -fwrapv but are not specified as such in the
# CMakeLists.txt. Generated using `grep "fwrapv" . -r -l`
20040409-1w.c
pr23047.c
20040409-3w.c
20040409-2w.c
930529-1.c
pr22493-1.c
920711-1.c
920612-1.c
"""

def parse_skipped_files(skipped_files):
    entries = set()
    for line in skipped_files.splitlines():
        line = line.strip()
        if '#' in line:
           line = line[:line.find('#')].strip()
        if line:
            entries.add(line)
    return entries

# skip files as listed in the CMakeLists.txt of the llvm test suite
# to keep it simple, we skip files that require special compiler flags, as well
# as any files that do not work with all architectures
SKIPPED_FILES_EXECUTE = parse_skipped_files(ignored_files_dir_execute)
SKIPPED_FILES_IEEE = parse_skipped_files(ignored_files_dir_ieee)

def emit_mk_dir(of, dstdir):
    of.write(f"mkdir -p {dstdir}\n")


def emit_clang_cmdline(of, clang, special_opts, arch, dstfile, srcfile, suffix):
    cmd_line = [clang]
    # Options every clang invocation gets
    cmd_line.extend(EXTRA_OPTIONS)
    # architecture specific args
    cmd_line.extend(ARCHMAP[arch])
    # special arguments for this specific invocation
    if special_opts:
        cmd_line.extend(special_opts)
    # Generate bitcode and not do a full compilation
    cmd_line.extend(["-o", str(dstfile.with_suffix(suffix)), str(srcfile)])

    # write out the cmdline
    of.write(" ".join(cmd_line))
    of.write("\n")


def emit_clang_bc_cmdline(of, clang, ld, arch, dstfile, srcfile):
    emit_clang_cmdline(
        of, clang, ["-emit-llvm", "-c"], arch, dstfile, srcfile, suffix=".bc"
    )


def emit_clang_elf_cmdline(of, clang, ld, arch, dstfile, srcfile):
    emit_clang_cmdline(of, clang, [f"-fuse-ld={ld}"], arch, dstfile, srcfile, suffix=".elf")


def emit_mkdir_command(
    of, source_file, source_dir_base, dest_dir_base, previous_dir=None
):
    # convert source filename to one in the destination directory tree
    idx = source_file.parts.index(source_dir_base)
    new_path = dest_dir_base.joinpath(*source_file.parts[idx + 1 :])

    # emit the mkdir command to create the output directory
    # skip emitting duplicates, since many files share the same subdirectory
    new_dir = new_path.parent
    if new_dir != previous_dir:
        emit_mk_dir(of, new_dir)
    return new_dir


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--clang",
        default="clang-11",
        help="Which clang binary to run, default clang-11",
    )
    parser.add_argument(
        "--ld",
        default="lld-11",
        help="Which linker to use, default lld-11",
    )
    parser.add_argument(
        "--source", default=f"{MYDIR}/../source", help="where to look for source files"
    )
    parser.add_argument(
        "--dest", default=f"{MYDIR}/../compiled", help="where to put output"
    )
    parser.add_argument(
        "--emit-bitcode",
        default=False,
        action="store_true",
        help="Emit commands to compile to bitcode",
    )
    parser.add_argument(
        "--emit-binaries",
        default=False,
        action="store_true",
        help="Emit commands to compile binaries",
    )
    parser.add_argument(
        "-o",
        "--outfile",
        default="/dev/stdout",
        help="Output file to write, default to stdout",
    )

    args = parser.parse_args()

    if not args.emit_bitcode and not args.emit_binaries:
        sys.stderr.write("Nothing to do.\n")
        sys.stderr.write("Please specify --emit-bitcode or --emit-binaries\n")
        sys.exit(1)

    if shutil.which(args.clang) is None:
        sys.stderr.write(f"Could not find clang command: {args.clang}\n")
        sys.exit(1)

    source_path = Path(args.source)
    # find every .c file
    sources = [s for s in source_path.glob("*.c") if s.name not in SKIPPED_FILES_EXECUTE]
    sources.extend([s for s in source_path.glob("ieee/*.c") if s.name not in SKIPPED_FILES_IEEE])

    # find the last part of the source path, so that we can replicate source tree in destination dir
    last_source_part = source_path.parts[-1]

    if 0 == len(sources):
        sys.stderr.write(f"Could not find any C source in {args.source}\n")
        sys.exit(1)

    worklist = []
    previous_dir = None

    output_styles = []
    emit_functions = {}
    if args.emit_bitcode:
        output_styles.append("bitcode")
        emit_functions["bitcode"] = emit_clang_bc_cmdline
    if args.emit_binaries:
        output_styles.append("binaries")
        emit_functions["binaries"] = emit_clang_elf_cmdline

    with open(args.outfile, "w") as of:
        for (arch, cmdargs) in ARCHMAP.items():
            for outstyle in output_styles:
                destination = Path(f"{args.dest}/{outstyle}/{arch}")
                for source in sources:
                    previous_dir = emit_mkdir_command(
                        of, source, last_source_part, destination, previous_dir
                    )
                    # save the arch/source/dest pair to avoid recomputing it
                    worklist.append(
                        (
                            arch,
                            previous_dir.joinpath(source.name),
                            source,
                            outstyle,
                        )
                    )

        # emit clang command lines to output bitcode
        for item in worklist:
            emit_functions[item[3]](
                of, args.clang, args.ld, arch=item[0], dstfile=item[1], srcfile=item[2]
            )

