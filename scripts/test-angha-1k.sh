#!/bin/bash
DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
SRC_DIR=$( cd "$( dirname "${DIR}" )" && pwd )

RELLIC_DECOMPILE="rellic-decomp-11.0"
function Help
{
  echo "Run Rellic on AnghaBech-1K"
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
		PYTHONPATH=${SRC_DIR}/libraries/lifting-tools-ci/tool_run_scripts \
		python3 -c "import stats,sys; s=stats.Stats(); s.load_json(sys.stdin); print(s.get_fail_count())" \
        < ${input_json})

    if [[ "${fail_msg}" != "0" ]]
    then
        echo "[!] There were [${fail_msg}] failures on ${arch}:"
		PYTHONPATH=${SRC_DIR}/libraries/lifting-tools-ci/tool_run_scripts \
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
mkdir -p rellic-angha-test-1k
pushd rellic-angha-test-1k

# fetch the test set: 1K bitcode (per arch)
${SRC_DIR}/libraries/lifting-tools-ci/datasets/fetch_anghabench.sh --run-size 1k --bitcode
# extract it
for tarfile in *.tar.xz
do
    tar -xJf ${tarfile}
done

FAILED="no"
for arch in $(ls -1 bitcode/)
do
    echo "[+] Testing architecture ${arch}"
    ${SRC_DIR}/libraries/lifting-tools-ci/tool_run_scripts/rellic.py \
        --rellic "${RELLIC_DECOMPILE}" \
        --input-dir "$(pwd)/bitcode/${arch}" \
        --output-dir "$(pwd)/results/${arch}" \
        --run-name "rellic-live-ci-${arch}" \
        --dump-stats
#        --test-options "${SRC_DIR}/ci/angha_1k_test_settings.json" \

    if ! check_test "$(pwd)/results/${arch}/stats.json"
    then
        echo "[!] Failed decompilation for ${arch}"
        FAILED="yes"
    fi
done

if [[ "${FAILED}" = "no" ]]
then
	echo "[+] All tests successful!"
    exit 0
fi

echo "[!] One or more failures encountered during test"
exit 1