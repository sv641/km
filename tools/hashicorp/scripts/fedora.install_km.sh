#!/bin/bash -xe
#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# assumes $storage with kkm,run and kontain.tar.gz
# installs kontain , kkm, docker

readonly storage="/tmp"

# Install KKM... it will also validate hardware.
# dnf install kernel-headers-$(uname -r) -y 2>&1
dnf install kernel-headers -y 2>&1
$storage/kkm.run 2>&1

# Install Docker
dnf install -y moby-engine

# Add Kontain to PATH
mkdir -p /opt/kontain && tar -C /opt/kontain -xzf $storage/kontain.tar.gz
echo "PATH=\$PATH:/opt/kontain/bin" >> ~vagrant/.bashrc

# Add gdb, we usually need it
dnf install -y gdb

# Configure Docker and restart
usermod -aG docker vagrant
mkdir /etc/docker && cp $storage/daemon.json /etc/docker/daemon.json
systemctl reload-or-restart docker.service

