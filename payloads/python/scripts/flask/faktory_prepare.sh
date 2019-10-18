#!/bin/bash
#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Python specific code running inside of a container based on original (to-be-coonverted) image.
#
# Copies python-related stuff to $1 (default /tmp/faktory) and generates shebang python3 file with proper path
# The results are used upstairs to construct proper Kontainer
#
# TODO
# - Maybe rewrite in python ? BASH is not on alpine; drop 'apk add bash' from makefiles

DIR=$1  # where to save files
if [ -z "$DIR" ] ; then DIR=/tmp/faktory; fi

# TODO: Issues: how do we find app if it's not workdir ? Need to analyize CMD/ENTRYPOINT. For now, assume WORKDIR Or param
APPDIR=$2 # where is the app
if [ -z "$APPDIR" ] ; then APPDIR=`pwd`; fi


msg() { echo "$@" >&2; }

msg We are in $APPDIR

# Copy all  py-related files into DIR
# Keep .pyc - they are  probably good to accelerate the start. '.so' we can filter out - they are mainly in lib-dynload
msg Copying .py-related files from python import dirs to $DIR...
# different formats of the same list - pydirs used in bash to tar stuff, pypath is useful in forming shebang file
pydirs=$(PYTHONPATH=$APPDIR python3 -c 'import sys; print(" ".join([i[1:] for i in sys.path if i and not i.endswith("lib-dynload")]))')
pypath=$(PYTHONPATH=$APPDIR python3 -c 'import sys; print("/" + ":".join([i for i in sys.path if i and not i.endswith("lib-dynload")])[1:])')
mkdir -p ${DIR}/pydirs
cd /
for d in $pydirs ; do
   msg -e " \t$d -> $DIR/$d"
   if [ -z "$d" -o ! -e "$d"  ] ; then msg -e " \t...$d is not there, skipping.";  continue ; fi
   tar c -f - $d  | tar x -C $DIR/pydirs -f -
done

# create shebang file with properly formed PYTHONPATH
msg Creating $DIR/python3 with PYTHONPATH=$pypath
cat <<EOF > $DIR/python3
#!/km --putenv=PYTHONPATH=$pypath
#
# This file is calling Kontain Monitor (KM) and passing it's own name (e.g. python3) to KM as a payload file name.
# The KM will first add '.km' to the payload file name, and end up using 'python3.km'.
# So invoking this script with params (e.g. 'python3 myapp.py') will actually call
# '/km --putenv=PYTHONPATH=$pypath python3.km myapp.py'
#
EOF
chmod a+x $DIR/python3