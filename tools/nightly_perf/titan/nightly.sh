#!/bin/bash

#set -e

export MAX_NODES=1

root_dir="$(dirname "${BASH_SOURCE[0]}")"
cd "${root_dir}"

if [[ "${LG_RT_DIR}" == "" ]]
then
  echo LG_RT_DIR is not defined
  exit -1
fi

cat nightly.script \
	| sed -e "s/__MAX_NODES__/${MAX_NODES}/" \
	| sed -e "s/__TEST_ARGUMENTS__/--perf_max_nodes=1 --perf_min_nodes=1/" \
	| sed -e "s:__LG_RT_DIR__:${LG_RT_DIR}:g" \
	> n.script

mkdir -p log
pushd log
qsub ../n.script
popd

