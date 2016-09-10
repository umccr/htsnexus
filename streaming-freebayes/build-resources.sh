#!/bin/bash
#
# sudo this script to update the samtools & freebayes executables under resources/

export SAMTOOLS_REVISION=1.3.1
export FREEBAYES_REVISION=v1.0.2

HERE="${BASH_SOURCE[0]}"
while [ -h "$HERE" ] ; do HERE="$(readlink "$HERE")"; done
HERE="$( cd -P "$( dirname "$HERE" )" && pwd )"
export HERE

docker run -t -i --rm \
  -v "${HERE}/resources:/io" \
  ubuntu:trusty \
  bash -c "set -ex -o pipefail
           apt-get -qq update
           DEBIAN_FRONTEND=noninteractive apt-get -qq install -y --no-install-recommends --no-install-suggests ca-certificates make cmake git g++ zlib1g-dev libncurses-dev
           git clone -b ${SAMTOOLS_REVISION} --recursive https://github.com/samtools/htslib.git
           git clone -b ${SAMTOOLS_REVISION} --recursive https://github.com/samtools/samtools.git
           make -j $(nproc) -C samtools samtools
           mkdir -p /io/usr/local/bin
           cp samtools/samtools /io/usr/local/bin
           git clone -b ${FREEBAYES_REVISION} --recursive https://github.com/ekg/freebayes.git
           make -j $(nproc) -C freebayes
           make -j $(nproc) -C freebayes/vcflib vcfuniq vcfstreamsort
           cp freebayes/bin/freebayes freebayes/vcflib/bin/vcffirstheader freebayes/vcflib/bin/vcfstreamsort freebayes/vcflib/bin/vcfuniq /io/usr/local/bin"

chown -R "$SUDO_USER" $HERE/resources
