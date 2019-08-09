#!/usr/bin/env python2
# -*- coding: utf-8 -*-

import sys

with open(sys.argv[1], 'r') as f:
    for l in f.readlines():
        print "    \"%s\\n\"" % l.strip()