#!/bin/bash
root_dir="$(dirname "${BASH_SOURCE[0]}")"
cd "${root_dir}"
echo root dir is ${root_dir}

unset LG_RT_DIR
USE_GASNET=1 RDIR=auto CONDUIT=gemini CC=gcc CXX=CC HOST_CC=gcc HOST_CXX=g++ ./setup_env.py
##RDIR=auto CONDUIT=gemini CC="cc -dynamic" CXX="CC -dynamic" HOST_CC="cc -dynamic" HOST_CXX="CC -dynamic" ./setup_env.py
