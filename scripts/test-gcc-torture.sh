#!/usr/bin/env bash
DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
SRC_DIR=$( cd "$( dirname "${DIR}" )" && pwd )
TEST_SUITE_NAME=gcc-torture
HOST_ARCH=$( uname -m )
case $HOST_ARCH in
  x86_64)
    HOST_ARCH=amd64
  ;;
esac

# disabled by default
ROUNDTRIP_ENABLED="false"

LLVM_VERSION=15
RELLIC_DECOMPILE="rellic-decomp-${LLVM_VERSION}.0"
function Help
{
  echo "Run Rellic on GCC C torture test suite"
  echo ""
  echo "Options:"
  echo "  --rellic-cmd <cmd>     The rellic decompile command to invoke. Default ${RELLIC_DECOMPILE}"
  echo "  -h --help              Print help."
}

function check_test
{
    local input_json=${1}
    if [[ ! -f ${1} ]]
    then
        echo "[!] Could not find python results for: ${input_json}"
        return 1
    fi

    # count number of failures
    fail_msg=$(\
		PYTHONPATH=${SRC_DIR}/external/lifting-tools-ci/tool_run_scripts \
		python3 -c "import stats,sys; s=stats.Stats(); s.load_json(sys.stdin); print(s.get_fail_count())" \
        < ${input_json})

    if [[ "${fail_msg}" != "0" ]]
    then
        echo "[!] There were [${fail_msg}] failures on ${arch}:"
		PYTHONPATH=${SRC_DIR}/external/lifting-tools-ci/tool_run_scripts \
        python3 -c "import stats,sys; s=stats.Stats(); s.load_json(sys.stdin); s.print_fails()" \
			< ${input_json}
        return 1
    fi

    return 0
}

set -euo pipefail

while [[ $# -gt 0 ]] ; do
    key="$1"

    case $key in

        -h)
            Help
            exit 0
        ;;

        --help)
            Help
            exit 0
        ;;


        # Cmd to run for decompilation
        --rellic-cmd)
            RELLIC_DECOMPILE=${2}
            shift # past argument
        ;;

        *)
            # unknown option
            echo "[x] Unknown option: ${key}"
            exit 1
        ;;
    esac

    shift # past argument or value
done


if ! ${RELLIC_DECOMPILE} --version &>/dev/null;
then   
    echo "[!] Could not execute rellic cmd: ${RELLIC_DECOMPILE}"
    exit 1
fi

# create a working directory
mkdir -p rellic-${TEST_SUITE_NAME}
pushd rellic-${TEST_SUITE_NAME}

# fetch the test set
${SRC_DIR}/external/lifting-tools-ci/datasets/fetch_gcc_torture.sh --bitcode --clang ${LLVM_VERSION}
# extract it
for tarfile in *.tar.xz
do
    tar -xJf ${tarfile}
done

FAILED="no"
for arch in $(ls -1 bitcode/)
do
    echo "[+] Testing architecture ${arch}"
    ${SRC_DIR}/external/lifting-tools-ci/tool_run_scripts/rellic.py \
        --rellic "${RELLIC_DECOMPILE}" \
        --input-dir "$(pwd)/bitcode/${arch}" \
        --output-dir "$(pwd)/decompile/${arch}" \
        --run-name "rellic-live-ci-${arch}" \
        --test-options "${SRC_DIR}/ci/${TEST_SUITE_NAME}_test_settings.json" \
        --dump-stats

    if ! check_test "$(pwd)/decompile/${arch}/stats.json"
    then
        echo "[!] Failed decompilation for ${arch}"
        FAILED="yes"
    fi

    # if host arch, attempt roundtrip
    if [[ "$ROUNDTRIP_ENABLED" = "true" && "${HOST_ARCH}" = "$arch" ]]; then
      # FIXME: Not sure how useful this is. In its current implementation, test
      # runs will take a lot of time since it's doing 5 roundtrips per file on a
      # list of roughly 1.4k files sequentially.
      echo "testing roundtrip"
      ${DIR}/roundtrip.py \
        "${RELLIC_DECOMPILE}" \
        "$(pwd)/decompile/${arch}" \
        "clang-${LLVM_VERSION}" \
        --timeout 30 \
        --recurse-subdirs
    else
      # otherwise, just try compiling
      mkdir -p "$(pwd)/recompile/${arch}"
      ${SRC_DIR}/external/lifting-tools-ci/tool_run_scripts/recompile.py \
          --clang "clang-${LLVM_VERSION}" \
          --input-dir "$(pwd)/decompile/${arch}" \
          --output-dir "$(pwd)/recompile/${arch}" \
          --run-name "recompile-live-ci-${arch}" \
          --dump-stats
    fi

done

if [[ "${FAILED}" = "no" ]]
then
	echo "[+] All tests successful!"
    exit 0
fi

echo "[!] One or more failures encountered during test"
exit 1
