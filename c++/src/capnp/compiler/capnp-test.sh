#! /bin/sh
# Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Tests the `capnp` tool's various commands, other than `compile`.

set -eu

fail() {
  echo "FAILED: $@" >&2
  exit 1
}

if test -e ./capnp; then
  CAPNP=${CAPNP:-./capnp}
else
  CAPNP=${CAPNP:-capnp}
fi

SCHEMA=$(dirname "$0")/../test.capnp
TESTDATA=$(dirname "$0")/../testdata

$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/binary || fail encode
$CAPNP encode --flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/flat || fail encode flat
$CAPNP encode --packed $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packed || fail encode packed
$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/pretty.txt | cmp $TESTDATA/binary || fail parse pretty

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/pretty.txt || fail decode
$CAPNP decode --flat $SCHEMA TestAllTypes < $TESTDATA/flat | cmp $TESTDATA/pretty.txt || fail decode flat
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/packed | cmp $TESTDATA/pretty.txt || fail decode packed
$CAPNP decode --short $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/short.txt || fail decode short

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/segmented | cmp $TESTDATA/pretty.txt || fail decode segmented
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/segmented-packed | cmp $TESTDATA/pretty.txt || fail decode segmented-packed

test_eval() {
  test "x$($CAPNP eval $SCHEMA $1)" = "x$2" || fail eval "$1 == $2"
}

test_eval TestDefaults.uInt32Field 3456789012
test_eval TestDefaults.structField.textField '"baz"'
test_eval TestDefaults.int8List "[111, -111]"
test_eval 'TestDefaults.structList[1]' '( textField = "structlist 2" )'
test_eval globalStruct '(int32Field = 54321)'
test_eval TestConstants.enumConst corge
test_eval 'TestListDefaults.lists.int32ListList[2][0]' 12341234

$CAPNP compile -ofoo $TESTDATA/errors.capnp.nobuild 2>&1 | sed -e "s,^.*/errors[.]capnp[.]nobuild,file,g" |
    cmp $TESTDATA/errors.txt || fail error output
