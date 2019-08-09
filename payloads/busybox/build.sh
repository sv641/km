#!/bin/bash

src=busybox
set -x
set -e

if [ ! -d $src ]; then
    git clone https://github.com/mirror/busybox.git $src
fi

cp config.txt $src/.config
make -C $src -j16
cp $src/busybox $src/busybox.km