# Prism

Research prototype

All sources are published under Apache2 license unless there is no license text on top of the file.

## How to run benchmark applications

### Setup Vagrant environment

#### Requirements

- Vagrant 2.2.10 or above (only tested on Vagrant 2.2.10)
- [vagrant-libvirt](https://github.com/vagrant-libvirt/vagrant-libvirt)
- [vagrant-reload](https://github.com/aidanns/vagrant-reload)

In addition to this, you need approx 50GB of storage for VM images. If
your libvirt image volume (usually /var/lib/libvirt/images) doesn't have
enough volume, please prepare the capacity (e.g. Symlink the external
storage mount point to /var/lib/libvirt/images or change the image volume
location. c.f. 
https://www.unixarena.com/2015/12/linux-kvm-change-libvirt-vm-image-store-path.html/)

#### Procedure

```
# On top of this repo
vagrant up --provider libvirt --no-parallel
```

We observe sometimes the Vagrant fails to provision the nodes unexpectedly. In that case, destroy
the nodes and retrying usually worked for us.

Please do not remove `--no-parallel` since our Vagrant provisioner heavily use computing power of
our external storage.

### Setup Baremetal environment

See the Vagrantfile for required topology and provisioning procedure. The Linux distribution version
should be the same as the one used in the Vagrant base box.

### Run `phttp-bench` application

`phttp-bench` is an application which is useful for measuring the effect of the TCP handoff. Client specifies the sizeof the object to download by path like `/1000` . The unit is byte. Frontend just handoff the requests to the backends without any processing and the backends send the response with on-memory binary blob.

#### Setup switch

```
# On switch node
cd /home/vagrant/Prism-HTTP/switch
sudo ./bin/prism_switchd -s vale0 -I $(pwd)/include -f src/cpp/prism_switch.bpf.c -a 172.16.10.10:18080
```

#### Setup frontend

```
# On frontend1 node

# HTTP
sudo phttp-bench-proxy --addr 172.16.10.11 --port 80 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --nworkers 1

# HTTPS (Please replace server.crt/key to your own one)
openssl req -x509 -sha256 -nodes -days 3650 -newkey rsa:2048 -subj /CN=localhost -keyout server.key -out server.crt
sudo phttp-bench-proxy --addr 172.16.10.11 --port 443 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --tls --tls-crt server.crt --tls-key server.key --nworkers 1
```

#### Setup backends

```
# On backend1 node

# HTTP
sudo phttp-bench-backend --addr 172.16.10.12 --port 80 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --nworkers 1

# HTTPS
sudo phttp-bench-backend --addr 172.16.10.12 --port 443 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --nworkers 1

# On backend2 node

# HTTP
sudo phttp-bench-backend --addr 172.16.10.13 --port 80 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --nworkers 1

# HTTPS
sudo phttp-bench-backend --addr 172.16.10.13 --port 443 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --nworkers 1
```

#### Run test

```
# On client node
curl http://172.16.10.11/1000  # Download the 1K objects
```

### Run `phttp-kvs` application

`phttp-kvs` is a simple REST based object storage application. The object will be **sharded**.

#### Setup switch

```
# On switch node
cd /home/vagrant/Prism-HTTP/switch
sudo ./bin/prism_switchd -s vale0 -I $(pwd)/include -f src/cpp/prism_switch.bpf.c -a 172.16.10.10:18080
```

#### Setup frontend

```
# On frontend1 node

# HTTP
sudo phttp-kvs-proxy --addr 172.16.10.11 --port 80 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --nworkers 1

# HTTPS (Please replace server.crt/key to your own one)
openssl req -x509 -sha256 -nodes -days 3650 -newkey rsa:2048 -subj /CN=localhost -keyout server.key -out server.crt
sudo phttp-kvs-proxy --addr 172.16.10.11 --port 443 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --tls --tls-crt server.crt --tls-key server.key --nworkers 1
```

#### Setup backends

```
# Common
mkdir /tmp/prism-leveldb

# On backend1 node

# HTTP
sudo phttp-kvs-backend --addr 172.16.10.12 --port 80 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --nworkers 1 --dbdir /tmp/prism-leveldb

# HTTPS
sudo phttp-kvs-backend --addr 172.16.10.12 --port 443 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --nworkers 1 --dbdir /tmp/prism-leveldb

# On backend2 node

# HTTP
sudo phttp-kvs-backend --addr 172.16.10.13 --port 80 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --nworkers 1 --dbdir /tmp/prism-leveldb

# HTTPS
sudo phttp-kvs-backend --addr 172.16.10.13 --port 443 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --nworkers 1 --dbdir /tmp/prism-leveldb
```

#### Run test

```
# On client node
curl -X PUT http://172.16.10.11/foo -d "foofoofoo"  # PUT object
curl -X GET http://172.16.10.11/foo  # GET object
curl -X DELETE http://172.16.10.11/foo  # DELETE object
```

### Run `phttp-kvs-repl` application

`phttp-kvs-repl` is a simple REST based object storage application. The object will be **replicated**.

#### Setup switch

```
# On switch node
cd /home/vagrant/Prism-HTTP/switch
sudo ./bin/prism_switchd -s vale0 -I $(pwd)/include -f src/cpp/prism_switch.bpf.c -a 172.16.10.10:18080
```

#### Setup frontend

```
# On frontend1 node

# HTTP
sudo phttp-kvs-repl-proxy --addr 172.16.10.11 --port 80 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --nworkers 1

# HTTPS (Please replace server.crt/key to your own one)
openssl req -x509 -sha256 -nodes -days 3650 -newkey rsa:2048 -subj /CN=localhost -keyout server.key -out server.crt
sudo phttp-kvs-repl-proxy --addr 172.16.10.11 --port 443 --mac 02:00:00:00:00:01 --backlog 8192 --ho-addr 172.16.10.11 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --backends 172.16.10.12:8080,172.16.10.13:8080 --tls --tls-crt server.crt --tls-key server.key --nworkers 1
```

#### Setup backends

```
# On backend1 node

# HTTP
sudo phttp-kvs-repl-backend --addr 172.16.10.12 --port 80 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --next-server-addr 172.16.10.13 --next-server-port 8080 --nworkers 1

# HTTPS
sudo phttp-kvs-repl-backend --addr 172.16.10.12 --port 443 --mac 02:00:00:00:00:02 --backlog 8192 --ho-addr 172.16.10.12 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --next-server-addr 172.16.10.13 --next-server-port 8080 --nworkers 1

# On backend2 node

# HTTP
sudo phttp-kvs-repl-backend --addr 172.16.10.13 --port 80 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --next-server-addr 0.0.0.0 --next-server-port 0 --nworkers 1

# HTTPS
sudo phttp-kvs-repl-backend --addr 172.16.10.13 --port 443 --mac 02:00:00:00:00:03 --backlog 8192 --ho-addr 172.16.10.13 --ho-port 8080 --ho-backlog 64 --sw-addr 172.16.10.10 --sw-port 18080 --proxy-addr 172.16.10.11 --proxy-port 8080 --tls --tls-crt /dev/null --tls-key /dev/null --next-server-addr 0.0.0.0 --next-server-port 0 --nworkers 1
```

#### Run test

```
# On client node
curl -X PUT http://172.16.10.11/foo -d "foofoofoo"  # PUT object
curl -X GET http://172.16.10.11/foo  # GET object
# Delete operation is not implemented yet
```
