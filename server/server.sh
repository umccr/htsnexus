#!/bin/bash -e

BAMNEXUS_HOME="${BASH_SOURCE[0]}"
while [ -h "$BAMNEXUS_HOME" ] ; do BAMNEXUS_HOME="$(readlink "$BAMNEXUS_HOME")"; done
BAMNEXUS_HOME="$( cd -P "$( dirname "$BAMNEXUS_HOME" )" && pwd )"
export BAMNEXUS_HOME

cd "$BAMNEXUS_HOME"

if ! [ -f "runtime/node/bin/node" ]; then
    echo "Please run: make -C '$BAMNEXUS_HOME'"
    exit 1
fi

runtime/node/bin/node src/main.js "$@"
