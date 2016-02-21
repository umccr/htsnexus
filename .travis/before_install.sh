#!/bin/bash
set -ex -o pipefail

add-apt-repository -y ppa:ubuntu-toolchain-r/test # gcc 4.9
apt-get -qq update

apt-get -qq install -y gcc-4.8 g++-4.8 binutils
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 60 \
                    --slave /usr/bin/g++ g++ /usr/bin/g++-4.8 \
                    --slave /usr/bin/gcov gcov /usr/bin/gcov-4.8
