#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

set -euo pipefail

function show_usage {

  printf "${0}: Compress bitcode to a form that can be uploaded"
  printf "Usage (default values in [brackets]):\n"
  printf "\n"
  printf "\t--help: this screen\n"
  printf "\t--clang ##: Which clang this bitcode was built with\n"
  return 0
}

CLANG=unknown

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
  echo "Please specify a clang version via --clang # (e.g. --clang 12)"
  exit 1
fi

pushd "${DIR}/build/compiled" &>/dev/null

for otype in bitcode binaries
do
  for archdir in ${otype}/*
  do
    arch=$(basename ${archdir})
    echo "Compressing ${otype} ${arch}"
    XZ_OPT=-e9 tar -Ipixz -cf ${otype}.${CLANG}.${arch}.tar.xz ${otype}/${arch}
  done
done

popd &>/dev/null
