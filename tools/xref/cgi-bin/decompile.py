#!/usr/bin/env python3

#
# Copyright (c) 2022-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

import cgi
import cgitb
import sys
import subprocess
cgitb.enable()

form = cgi.FieldStorage()
fileitem = form["filename"]

if fileitem.file:
    rellic = "rellic-xref"
    res = subprocess.run(
        ["/usr/bin/env",
            rellic,
            "--output-http=1",
            "--remove-phi-nodes=1",
            "--lower-switch=1",
            "--input=/dev/stdin",
            "--output=/dev/stdout"],
        input=fileitem.file.read(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=5)
    sys.stdout.buffer.write(res.stdout)
    sys.stdout.flush()
else:
    print("Content-Type: text/plain")
    print("")
    print("Invalid request")