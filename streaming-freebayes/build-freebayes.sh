#!/bin/bash
#
# sudo this script to update the freebayes executables in this directory

set -ex -o pipefail
export FREEBAYES_REVISION=v1.0.2
docker run -t -i --rm \
  -v `pwd`:/io \
  ubuntu:trusty \
  bash -c "set -ex -o pipefail
           apt-get -qq update
           DEBIAN_FRONTEND=noninteractive apt-get -qq install -y --no-install-recommends --no-install-suggests ca-certificates make cmake git g++ zlib1g-dev
           git clone -b ${FREEBAYES_REVISION} --recursive https://github.com/ekg/freebayes.git
           make -j $(nproc) -C freebayes
           cp freebayes/bin/freebayes freebayes/vcflib/bin/vcffirstheader freebayes/bamtools/bin/bamtools freebayes/bamtools/lib/libbamtools.so.2.3.0 /io"
