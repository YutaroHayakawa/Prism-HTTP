#!/bin/bash

apt-get update
apt-get -y install build-essential bc flex bison libssl-dev libelf-dev

cd $BUILD_ROOT

wget https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net-next.git/snapshot/net-next-0db0561d13df07978bea63a19f644fc16a60f54a.tar.gz
tar xvf net-next-0db0561d13df07978bea63a19f644fc16a60f54a.tar.gz
cd net-next-0db0561d13df07978bea63a19f644fc16a60f54a
make olddefconfig
make -j $NWORKERS deb-pkg
