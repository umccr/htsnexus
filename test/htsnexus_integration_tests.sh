#!/bin/bash
set -e -o pipefail

if [ -z "$TMPDIR" ]; then
	export TMPDIR=/tmp
fi

HTSNEXUS_HOME="${BASH_SOURCE[0]}"
while [ -h "$HTSNEXUS_HOME" ] ; do HTSNEXUS_HOME="$(readlink "$HTSNEXUS_HOME")"; done
HTSNEXUS_HOME="$( cd -P "$( dirname "$HTSNEXUS_HOME" )" && pwd )/.."
export HTSNEXUS_HOME
export BASH_TAP_ROOT="${HTSNEXUS_HOME}/test"

make -C "${HTSNEXUS_HOME}/server"
pushd "${HTSNEXUS_HOME}/indexer"
cmake . && make
popd

prove -v "${HTSNEXUS_HOME}/test/htsnexus_integration.t"
