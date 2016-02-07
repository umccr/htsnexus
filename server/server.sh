#!/bin/bash -e

HTSNEXUS_HOME="${BASH_SOURCE[0]}"
while [ -h "$HTSNEXUS_HOME" ] ; do HTSNEXUS_HOME="$(readlink "$HTSNEXUS_HOME")"; done
HTSNEXUS_HOME="$( cd -P "$( dirname "$HTSNEXUS_HOME" )" && pwd )"
export HTSNEXUS_HOME

cd "$HTSNEXUS_HOME"

if ! [ -f "runtime/node/bin/node" ]; then
    echo "Please run: make -C '$HTSNEXUS_HOME'"
    exit 1
fi

runtime/node/bin/node src/main.js "$@"
