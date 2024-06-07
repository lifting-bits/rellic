#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# stop on failure
set -euo pipefail

function show_usage {

  printf "${0}: Build bitcode and binaries for all architectures for clang's gcc
  torture test suite"
  printf "Usage (default values in [brackets]):\n"
  printf "\n"
  printf "\t--help: this screen\n"
  printf "\t--clang ##: Which clang this bitcode was built with\n"
  printf "\t--ld LINKER: Which linker to use for linking the binaries\n"
  return 0
}

CLANG=unknown
LD=unknown

while [[ $# -gt 0 ]]
do
    key="$1"
    case $key in
        --help | -h | -?)
        show_usage ${0}
        exit 0
        ;;
        --clang)
        shift
        CLANG=clang-${1}
	CLANG_VERSION=${1}
        shift
        ;;
        --ld)
        shift
        LD=${1}
        shift
        ;;
        *)    # unknown option
        echo "UNKNOWN OPTION: ${1}"
        exit 1
        shift # past argument
        ;;
    esac
done

if [[ "${CLANG}" = "unknown" ]]
then
  echo "Please specify a clang version via --clang # (e.g. --clang 15)"
  exit 1
fi

if [[ "${LD}" = "unknown" ]]
then
  echo "Please specify a linker via --ld LINKER (e.g. --ld lld-15)"
  exit 1
fi

mkdir -p "${DIR}/build"
pushd "${DIR}/build" &>/dev/null
# download only the part of the llvm test suite that is required, otherwise
# cloning might take several minutes
TEST_SUITE_DIR="llvm-test-suite-${CLANG_VERSION}"
if [ ! -d "${TEST_SUITE_DIR}" ]; then
  echo "retrieving llvm-test-suite"
  git clone \
    --branch "release/${CLANG_VERSION}.x" \
    --depth 1 \
    --filter=blob:none \
    --sparse \
    https://github.com/llvm/llvm-test-suite.git \
    "${TEST_SUITE_DIR}"

  (cd "${TEST_SUITE_DIR}" && git sparse-checkout set "SingleSource/Regression/C/gcc-c-torture")
else
  echo "llvm-test-suite already downloaded"
fi

compile_commands=$(mktemp /tmp/compile_commands.XXXXXX)
echo "generating compile commands in tmp file ${compile_commands}"
"${DIR}/generate_compile_commands.py" \
  --clang "${CLANG}" \
  --ld "${LD}" \
  --source "${TEST_SUITE_DIR}/SingleSource/Regression/C/gcc-c-torture/execute" \
  --emit-bitcode \
  --emit-binaries \
  --dest ./compiled \
  > "${compile_commands}"

# execute the mkdir commands sequentially first
grep "mkdir -p" "${compile_commands}" | bash

# run the compile commands in parallel since they are >10k for all
# architectures combined
echo "Executing compile commands in parallel"
grep "mkdir -p" "${compile_commands}" --invert-match \
  | xargs --delimiter '\n' --max-procs=0 --max-args=1 bash -c

# Executing sequentially is quite slow, prefer using xargs
# echo "Executing compile commands ${compile_commands}"
# bash "${compile_commands}"

# compressing results for upload
"${DIR}/compress_bitcode.sh" --clang "${CLANG_VERSION}"

echo "results can be found in ${DIR}/build"
