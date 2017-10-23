#!/bin/bash

root_dir="$(dirname "${BASH_SOURCE[0]}")"

module unload PrgEnv-pgi
module load PrgEnv-gnu
module load python

export CC=gcc
export CXX=CC
export HOST_CC=gcc
export HOST_CXX=g++
export USE_GASNET=1
export CONDUIT=gemini
export RDIR=auto

unset LG_RT_DIR

"$root_dir/../../../language/scripts/setup_env.py" --prefix="$root_dir"
