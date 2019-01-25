#!/usr/bin/env bash
#
# Copyright 2017, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

if [ 0$V -ge 3 ]; then
    set -x
fi

set -e

xmllint "$@" 2> >(grep -v --regexp='validates$' 1>&2)
