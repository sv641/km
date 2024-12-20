#!/bin/bash
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

# A little script to get needed linux packages, change podman config files, and add a small
# kontain selinux policy to allow podman to run containers using krun and km.
# Can also be invoke with -u flag to uninstall our changes

# exit if any command fails, display the commands if TRACE has a value.
set -e ; [ "$TRACE" ] && set -x

# Should be run as root.
[ `id -u` != "0" ] && echo "Must run as root" && exit 1

# Set variables to know what kind of system this is.
. /etc/os-release
[ "$ID" != "fedora" -a "$ID" != "ubuntu" -a "$ID" != "amzn" ] && echo "Unsupported linux distribution: $ID" && exit 1

# Some programs we care about
KRUN_PATH="/opt/kontain/bin/krun"
KM_PATH="/opt/kontain/bin/km"
RUNTIME_NAME="krun"
PODMAN_EXISTED=/opt/kontain/config/podman_existed

for arg in "$@"
do
   case "$arg" in
        -u)
            UNINSTALL=yes
        ;;
        --runtime-name=*)
            RUNTIME_NAME="${1#*=}"
        ;;
        --runtime-path=*)
            KRUN_PATH="${1#*=}"
            KM_PATH=$(echo "${KRUN_PATH}" | sed 's/krun/km/')
        ;;
    esac
    shift
done

# check that KRUN_PATH points to an executable file
if [ ! -x "$KRUN_PATH" ] && [ -z "$UNINSTALL" ]; then
   echo "Runtime path must be full path to an existing krun executable"
   exit 1
fi

# check that KM_PATH points to an executable file
if [ ! -x "$KM_PATH" ] && [ -z "$UNINSTALL" ]; then
   echo "KM execuable is not found or is not executable"
   exit 1
fi

# selinux policy related
GETENFORCEPATH=/usr/sbin/getenforce
KM_SELINUX_CONTEXT="system_u:object_r:bin_t:s0"
KONTAIN_SELINUX_POLICY=kontain_selinux_policy
POLDIR=/tmp/$KONTAIN_SELINUX_POLICY
KKM_DEVICE=/dev/kkm

# Podman configuration related
DOCKER_INIT=/bin/tini-static

# if running under sudo, update the invokers containers.conf, not root's
# Note that the shell nazi's think eval to expand ~ is evil but I can't find a better way
HOME_CONTAINERS_CONF=`eval echo ~${SUDO_USER}/.config/containers/containers.conf`
mkdir -p `dirname $HOME_CONTAINERS_CONF`

# Needed packages
# Note that docker_config.sh does not install the packages needed by krun. It depends on podman_config.sh doing it.
readonly FEDORA_PACKAGES="podman selinux-policy-devel yajl libcap libseccomp openssl-libs tini-static"
# libseccomp libcap openssl-libs are protected, at least on Fedora
readonly CLEAN_FEDORA_PACKAGES="podman selinux-policy-devel yajl-devel tini-static"
readonly UBUNTU_PACKAGES="podman libyajl2 libcap2 libseccomp2 openssl tini"
readonly CLEAN_UBUNTU_PACKAGES="podman libyajl2 tini"
readonly REFRESH_UBUNTU_PACKAGES="persistent-apt-get update"
readonly REFRESH_FEDORA_PACKAGES=true
readonly INSTALL_UBUNTU_PACKAGES="persistent-apt-get install -y "
readonly INSTALL_FEDORA_PACKAGES="dnf install -y "
readonly UNINSTALL_UBUNTU_PACKAGES="persistent-apt-get autoremove -y "
readonly UNINSTALL_FEDORA_PACKAGES="dnf remove -y "

# A small function to keep trying apt-get until other apt-get'ers finish.
function persistent-apt-get
{
    eflag=0
    if echo $- | grep -q "e"; then eflag=1; fi

    MESSAGE="E: Could not get lock /var/lib/dpkg/lock-frontend."
    APT_GET_OUTPUT=/tmp/apt-get-out-$$
    while true; do
        cp /dev/null $APT_GET_OUTPUT
        [ $eflag -ne 0 ] && set +e
        apt-get $* >$APT_GET_OUTPUT 2>&1
        rv=$?
        [ $eflag -ne 0 ] && set -e
        if test $rv -ne 0; then
            if grep -q "$MESSAGE" $APT_GET_OUTPUT; then
                # Another apt-get is running, try again.
                sleep 1
                continue
            fi
        fi
        break
    done
    cat $APT_GET_OUTPUT
    rm -f $APT_GET_OUTPUT
    return $rv
}

# UNINSTALL
if [ -n "$UNINSTALL" ]; then
    echo "Removing kontain related podman config changes"
    #remove specified config
    sed -i '/'"$RUNTIME_NAME"'/{:a;N;/]/!ba};//d' $HOME_CONTAINERS_CONF

    # check if there are Kontain runtimes left
    if grep -q /opt/kontain/bin/krun $HOME_CONTAINERS_CONF && [ ! -d "$PODMAN_EXISTED" ]; then

      # remove our selinux policy
      if [ "$ID" = "fedora" ]; then
         semodule --remove=$KONTAIN_SELINUX_POLICY
         # Remove the POLDIR?
         #rm -fr $POLDIR
         # We don't restore km's selinux context.
      fi

      # remove podman and selinux-policy-devel packages
      # We don't remove the packages needed by krun since many of them are usually present by default.
      UNINSTALL=UNINSTALL_${ID^^}_PACKAGES
      PACKAGES=CLEAN_${ID^^}_PACKAGES
      ${!UNINSTALL} ${!PACKAGES}
   fi
   exit 0
fi

# return success if selinux is present and enabled, otherwise return fail
function selinux_is_enabled()
{
    # If the "if" statement below is coded this way:
    # [ -e "${GETENFORCEPATH}" ] && [ "`${GETENFORCEPATH}`" != "Disabled" ]
    # the shell will complete the command line (including the `progname` stuff before the
    # test ("[") operands are evaluated.  If the getenforce program is missing then the
    # function fails because of that.
    if [ -e "${GETENFORCEPATH}" ]; then
        if [ "`${GETENFORCEPATH}`" != "Disabled" ]; then
            return 0
        fi
    fi
    return 1
}

# Install podman and policy build tools
echo "Installing podman and selinux packages"
PACKAGES=${ID^^}_PACKAGES
INSTALL=INSTALL_${ID^^}_PACKAGES
REFRESH=REFRESH_${ID^^}_PACKAGES

if command -v podman &> /dev/null
then
   mkdir -p "$PODMAN_EXISTED"
fi

# Tell ubuntu about the ppackage repo containing podman
if [ "$ID" = "ubuntu" ]
then
    persistent-apt-get update
    persistent-apt-get install -y wget gnupg2
    echo "deb http://download.opensuse.org/repositories/devel:/kubic:/libcontainers:/stable/xUbuntu_${VERSION_ID}/ /" > /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list
    wget -nv https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/xUbuntu_${VERSION_ID}/Release.key -O- | apt-key add -
fi
# install podman, selinux utils, krun dependencies
${!REFRESH}
${!INSTALL} ${!PACKAGES}

# Copy whatever they have even if we don't alter it.
if [ -e $HOME_CONTAINERS_CONF ]; then
    cp $HOME_CONTAINERS_CONF $HOME_CONTAINERS_CONF.kontainsave
fi

# If the user's containers.conf is missing give them a bare bones version
echo
if [ ! -e $HOME_CONTAINERS_CONF ]
then
    echo "$HOME_CONTAINERS_CONF does not exist, creating it"
   cat <<EOF >$HOME_CONTAINERS_CONF
[containers]
init_path = "$DOCKER_INIT"

[network]

[engine]

[engine.runtimes]
"$RUNTIME_NAME" = [
        "$KRUN_PATH"
]
EOF
    if test "$SUDO_USER" != ""
    then
        # kludge! fixup owner and group of each component of .config/containers/containers.conf
        # Yes, "chown -R u:g ~/.config" would do it but there can be lots of stuff in ~/.config
        # and I don't want to hit all of those directories and their contents.
        t=$HOME_CONTAINERS_CONF
        ug=`id -u $SUDO_USER`:`id -g $SUDO_USER`
        chown $ug $t
        t=`dirname $t`
        chown $ug $t
        t=`dirname $t`
        chown $ug $t
    fi
else
    # containers.conf does exist, fix its contents if needed.
    # set init_path in the [containers] section of ~/.config/containers/containers.conf
    echo "$HOME_CONTAINERS_CONF does exist, checking its contents"
    if ! grep -q "^ *init_path" $HOME_CONTAINERS_CONF
    then
        # no init_path, use the docker init
        sed --in-place -e "/^\[containers\]/ainit_path = \"$DOCKER_INIT\"" $HOME_CONTAINERS_CONF
    else
        if ! grep -q "^ *init_path *= *\"$DOCKER_INIT\"" $HOME_CONTAINERS_CONF
        then
            echo "Leaving init_path statement in $HOME_CONTAINERS_CONF unchanged, you may need to change it to $DOCKER_INIT"
            echo "Existing init_path:"
            grep "^ *init_path" $HOME_CONTAINERS_CONF
        else
            echo "init_path = \"$DOCKER_INIT\"" is already in $HOME_CONTAINERS_CONF
        fi
    fi

    # add runtime to ~/.config/containers/containers.conf at the beginning of the [engine.runtimes] section
    echo
    if ! grep -q "^ *$RUNTIME_NAME =" $HOME_CONTAINERS_CONF
    then
        sed --in-place -e "/^\[engine.runtimes\]/a$RUNTIME_NAME = [\n   \"$KRUN_PATH\",\n]\n" $HOME_CONTAINERS_CONF
    else
        echo "runtime krun already configured in $HOME_CONTAINERS_CONF"
        grep -A 3 "^ *krun =" $HOME_CONTAINERS_CONF
    fi
fi

# If the system has selinux enabled setup our policy
if selinux_is_enabled
then
    echo "selinux is present on this system"

    # set selinux context on /opt/kontain/bin/km
    if [ -e "$KM_PATH" ]; then
        chcon $KM_SELINUX_CONTEXT $KM_PATH
    else
        echo "$KM_PATH does not exist, when it does exist run the following:"
        echo "chcon $KM_SELINUX_CONTEXT $KM_PATH"
    fi

    # Add kontain selinux policy adjustments
    mkdir -p $POLDIR
    pushd $POLDIR || exit
   cat <<EOF >$KONTAIN_SELINUX_POLICY.te
module $KONTAIN_SELINUX_POLICY 1.0.0;

require {
  type container_t;
  type kvm_device_t;
  class chr_file { append getattr ioctl lock open read write };
}

allow container_t kvm_device_t:chr_file { append getattr ioctl lock open read write };

# You may get errors when this is "compiled".
# This is a known issue, see:
# https://bugzilla.redhat.com/show_bug.cgi?id=1861968
EOF

   cat <<EOF >$KONTAIN_SELINUX_POLICY.fc
$KKM_DEVICE		-c	gen_context(system_u:object_r:kvm_device_t,s0)
$KM_PATH		--	gen_context(system_u:object_r:bin_t,s0)
EOF

    ln -sf /usr/share/selinux/devel/Makefile
    make
    make reload
    restorecon -i -F $KKM_DEVICE $KM_PATH
    popd || exit
else
    echo "selinux not enabled on this system"
fi
