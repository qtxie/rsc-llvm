#!/bin/bash

# This script will install the llvm libs and gcc 11

set -eux

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root!"
   exit 1
fi

# install LLVM
bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
apt-get install -y liblld-13-dev

# install GCC 11
add-apt-repository ppa:ubuntu-toolchain-r/test
apt update && sudo apt install -y gcc-11 g++-11

update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110 --slave /usr/bin/g++ g++ /usr/bin/g++-11 --slave /usr/bin/gcov gcov /usr/bin/gcov-11 --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-11 --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-11

export LLVM_SYMBOLIZER_PATH=llvm-symbolizer-13