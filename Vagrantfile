# -*- mode: ruby -*-
# vi: set ft=ruby :

#
# Minimal network environment to make Prism work
#

#
# Privisioning
#
# Phase1: Install the kernel version independent packages and upgrade the kernel
# Phase2: Install the kernel version dependent packages like various kernel modules
# Phase3: Perform node type (Prism node, switch node ...) specific operations
#

#
# Prism nodes (frontend and backend nodes) requires the net-next kernel to
# use getsockopt(TLS_RX).
#
$provision_prism_node_phase1 = <<-EOS
echo "Installing dependencies (phase 1)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant
bash /home/vagrant/Prism-HTTP/scripts/install_deps.sh
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-image-5.9.0-rc1_5.9.0-rc1-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-headers-5.9.0-rc1_5.9.0-rc1-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-image-5.9.0-rc1-dbg_5.9.0-rc1-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-libc-dev_5.9.0-rc1-1_amd64.deb
dpkg -i linux-image-5.9.0-rc1_5.9.0-rc1-1_amd64.deb
dpkg -i linux-headers-5.9.0-rc1_5.9.0-rc1-1_amd64.deb
dpkg -i linux-image-5.9.0-rc1-dbg_5.9.0-rc1-1_amd64.deb
dpkg -i linux-libc-dev_5.9.0-rc1-1_amd64.deb
EOS

#
# Switch node requires Linux 4.18 to make netmap/VALE work correctly. We should
# be able to upgrade this to the latest kernel, but requires some update to our
# VALE module side.
#
$provision_switch_phase1 = <<-EOS
echo "Installing dependencies (phase 1)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant
bash /home/vagrant/Prism-HTTP/scripts/install_deps.sh
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-image-4.18.0_4.18.0-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-headers-4.18.0_4.18.0-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-image-4.18.0-dbg_4.18.0-1_amd64.deb
wget -nv https://web.sfc.wide.ad.jp/~river/packages/linux-libc-dev_4.18.0-1_amd64.deb
dpkg -i linux-image-4.18.0_4.18.0-1_amd64.deb
dpkg -i linux-headers-4.18.0_4.18.0-1_amd64.deb
dpkg -i linux-image-4.18.0-dbg_4.18.0-1_amd64.deb
dpkg -i linux-libc-dev_4.18.0-1_amd64.deb
EOS

#
# Install Creme and compile Prism switch library
#
$provision_prism_node_phase2 = <<-EOS
echo "Installing dependencies (phase2)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant

cd $BUILD_ROOT

#
# Install only headers. This version of netmap cannot be compiled with
# this version of kernel.
#
cd netmap
mkdir -p /usr/local/include/net
cp sys/net/* /usr/local/include/net/
cd ../

cd creme
make
cd ../

cd Prism-HTTP
cd switch
make
EOS

#
# Install Netmap and compile Prism switch
#
$provision_switch_phase2 = <<-EOS
echo "Installing dependencies (phase2)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant

cd $BUILD_ROOT

cd netmap
./configure --drivers=virtio_net.c
make -j $NPROC
make install
cd ../

cd Prism-HTTP
cd switch
make
EOS

#
# Load the netmap kernel module and create VALE switch. Change the VALE
# switching logic to eBPF based one.
#
$provision_switch_phase3 = <<-EOS
echo "Provisioning switch specific part (phase 3)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant

cd $BUILD_ROOT

cd netmap
insmod netmap.ko

# Don't do these separately. Otherwise, we'll lose the ssh connection.
rmmod virtio_net && insmod virtio_net.c/virtio_net.ko
cd ../

vale-ctl -a vale0:eth1
vale-ctl -a vale0:eth2
vale-ctl -a vale0:eth3
vale-ctl -a vale0:eth4

cd Prism-HTTP
cd switch
make
cd kern_src
NSRC=/home/vagrant/netmap make
insmod vale-bpf-native-vale0.ko
cd ../../
EOS

#
# Load the Creme kernel module and install Prism HTTP (phttp) server
#
$provision_prism_node_phase3 = <<-EOS
echo "Provisioning Prism node specific part (phase 3)"
export NWORKERS=`nproc`
export BUILD_ROOT=/home/vagrant

cd $BUILD_ROOT

cd creme
insmod creme.ko
cd ../

cd Prism-HTTP
cd src
make -j $NPROC
make install

# Need this work around because the current switch
# cannot respond to ARP request
ip neigh add 172.16.10.10 lladdr 02:00:00:00:00:00 dev eth1
EOS

def setup_file_provisioner(config)
  config.vm.provision "file",
    source: Dir.pwd() + "/src",
    destination: "/home/vagrant/Prism-HTTP/"
  config.vm.provision "file",
    source: Dir.pwd() + "/switch",
    destination: "/home/vagrant/Prism-HTTP/"
  config.vm.provision "file",
    source: Dir.pwd() + "/scripts",
    destination: "/home/vagrant/Prism-HTTP/"
end

#
# Create the L2 network topology like below
#
# +------------+ +------------+ +------------+ +------------+
# |frontend1   | |backend1    | |backend2    | |client      |
# +------------+ +------------+ +------------+ +------------+
#       |              |              |              |
#       |              |              |              |
# +---------------------------------------------------------+
# |switch                                                   |
# +---------------------------------------------------------+
#
# Network address: 172.16.10.0/24
#
# frontend1 : 172.16.10.11 02:00:00:00:00:01
# backend1  : 172.16.10.12 02:00:00:00:00:02
# backend2  : 172.16.10.13 02:00:00:00:00:03
# client    : 172.16.10.14 02:00:00:00:00:04
#
# In addition to this, switch node actually uses 172.16.10.10
# to listen on the UDP port to perform switch configuration
# operation. This will be configured by switch daemon process
# so we don't assign it in here.
#
Vagrant.configure("2") do |config|
  config.vm.box = "generic/ubuntu1804"

  config.vm.provider "libvirt" do |v|
    v.memory = 4096
    v.cpus = 2
  end

  # Copy source directory to VM. Shared filesystem doesn't work for custom kernel.
  config.vm.synced_folder ".", "/vagrant", disabled: true

  config.vm.define "frontend1" do |node|
    node.vm.hostname = "frontend1"
    node.vm.network "private_network",
      libvirt__network_name: "net0",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false,
      ip: "172.16.10.11",
      mac: "02:00:00:00:00:01"
    # Provision
    setup_file_provisioner(node)
    node.vm.provision "shell", inline: $provision_prism_node_phase1
    node.vm.provision "reload"
    node.vm.provision "shell", inline: $provision_prism_node_phase2
    node.vm.provision "shell", inline: $provision_prism_node_phase3
  end

  config.vm.define "backend1" do |node|
    node.vm.hostname = "backend1"
    node.vm.network "private_network",
      libvirt__network_name: "net1",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false,
      ip: "172.16.10.12",
      mac: "02:00:00:00:00:02"
    # Provision
    setup_file_provisioner(node)
    node.vm.provision "shell", inline: $provision_prism_node_phase1
    node.vm.provision "reload"
    node.vm.provision "shell", inline: $provision_prism_node_phase2
    node.vm.provision "shell", inline: $provision_prism_node_phase3
  end

  config.vm.define "backend2" do |node|
    node.vm.hostname = "backend2"
    node.vm.network "private_network",
      libvirt__network_name: "net2",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false,
      ip: "172.16.10.13",
      mac: "02:00:00:00:00:03"
    # Provision
    setup_file_provisioner(node)
    node.vm.provision "shell", inline: $provision_prism_node_phase1
    node.vm.provision "reload"
    node.vm.provision "shell", inline: $provision_prism_node_phase2
    node.vm.provision "shell", inline: $provision_prism_node_phase3
  end

  config.vm.define "client" do |node|
    node.vm.hostname = "client"
    node.vm.network "private_network",
      libvirt__network_name: "net3",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false,
      ip: "172.16.10.14",
      mac: "02:00:00:00:00:04"
  end

  config.vm.define "switch" do |node|
    node.vm.hostname = "switch"
    node.vm.network "private_network",
      libvirt__network_name: "net0",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false
    node.vm.network "private_network",
      libvirt__network_name: "net1",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false
    node.vm.network "private_network",
      libvirt__network_name: "net2",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false
    node.vm.network "private_network",
      libvirt__network_name: "net3",
      libvirt__forward_mode: "veryisolated",
      libvirt__dhcp_enabled: false
    # Provision
    setup_file_provisioner(node)
    node.vm.provision "shell", inline: $provision_switch_phase1
    node.vm.provision "reload"
    node.vm.provision "shell", inline: $provision_switch_phase2
    node.vm.provision "shell", inline: $provision_switch_phase3
  end
end
