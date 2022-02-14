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

passes = []
passes_select = form.getvalue("passes")
if isinstance(passes_select, list):
    passes = list(map(lambda x : f"--{x}=1", passes_select))
elif passes_select is not None:
    passes.append(f"--{passes_select}=1")

def get_tactic_list(elem):
    if isinstance(elem, list):
        return ','.join(elem)
    elif elem is not None:
        return elem
    else:
        return ''

cbr_tacticts = get_tactic_list(form.getvalue("cbr_tactics"))
sr_tactics = get_tactic_list(form.getvalue("sr_tactics"))

if fileitem.file:
    rellic = "rellic-xref"
    args = ["/usr/bin/env",
            rellic,
            "--output-http=1",
            "--remove-phi-nodes=1",
            "--lower-switch=1",
            "--input=/dev/stdin",
            "--output=/dev/stdout",
            f"--cbr-zcs-tactics={cbr_tacticts}",
            f"--sr-zcs-tactics={sr_tactics}"]
    args.extend(passes)
    timeout = form.getvalue("timeout")
    res = subprocess.run(args,
        input=fileitem.file.read(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout and float(timeout))
    sys.stdout.buffer.write(res.stdout)
    sys.stdout.flush()
else:
    print("Content-Type: text/plain")
    print("")
    print("Invalid request")