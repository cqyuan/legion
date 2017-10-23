#!/bin/bash

set -e

root_dir="$(dirname "${BASH_SOURCE[0]}")"
cd "$root_dir"

source "$root_dir"/build_vars.sh

export TERRA_DIR="$root_dir"/terra

source env.sh # defines PERF_ACCESS_TOKEN

export CI_RUNNER_DESCRIPTION="titan.ccs.ornl.gov"

export PERF_CORES_PER_NODE=12
export PERF_EXECUTION_DIR="$MEMBERWORK/csc103/nightly"
export PERF_REGENT_STANDALONE=1
export LAUNCHER="aprun"

../common/nightly.sh
