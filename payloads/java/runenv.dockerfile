# Copyright © 2020 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Create runenv for Java KM

# Use alpine for now. It only adds 6MB to Java's 170MB, and allows us to use 'ln -s' below
# TODO: switch to 'from scratch'. See https://github.com/kontainapp/km/issues/496
FROM alpine

ARG FROM=jdk-11.0.6+10/build/linux-x86_64-normal-server-release/images/jdk
ARG JAVA_DIR=/opt/kontain/java
ENV LD_LIBRARY_PATH ${JAVA_DIR}/lib/server:${JAVA_DIR}/lib/jli:${JAVA_DIR}/lib:/opt/kontain/runtime:/lib64

COPY jdk-11.0.6+10/build/linux-x86_64-normal-server-release/images/jdk/ ${JAVA_DIR}/
RUN mkdir /lib64; ln -s /opt/kontain/runtime/libc.so /lib64/ld-linux-x86-64.so.2

ENTRYPOINT [ "/opt/kontain/bin/km", "--copyenv", "/opt/kontain/java/bin/java.kmd" ]