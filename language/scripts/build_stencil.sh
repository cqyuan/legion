#!/bin/bash

set -e

root_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

mkdir "$1"
cd "$1"

SAVEOBJ=1 $root_dir/../regent.py $root_dir/../examples/stencil_fast.rg -fflow 1 -fflow-spmd 1 -fflow-spmd-shardsize 10 -fopenmp 0
mv stencil stencil.spmd10

cp $root_dir/../../bindings/terra/liblegion_terra.so .
cp $root_dir/../examples/libstencil.so .
cp $root_dir/../examples/libstencil_mapper.so .

cp $root_dir/../scripts/*_stencil.sh .