#! /bin/sh
# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Tests the `capnp` tool's various commands, other than `compile`.

set -eu

fail() {
  echo "FAILED: $@" >&2
  exit 1
}

if test -f ./capnp; then
  CAPNP=${CAPNP:-./capnp}
elif test -f ./capnp.exe; then
  CAPNP=${CAPNP:-./capnp.exe}
else
  CAPNP=${CAPNP:-capnp}
fi

SCHEMA=`dirname "$0"`/../test.capnp
TESTDATA=`dirname "$0"`/../testdata

$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/binary - || fail encode
$CAPNP encode --flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/flat - || fail encode flat
$CAPNP encode --packed $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packed - || fail encode packed
$CAPNP encode --packed --flat $SCHEMA TestAllTypes < $TESTDATA/short.txt | cmp $TESTDATA/packedflat - || fail encode packedflat
$CAPNP encode $SCHEMA TestAllTypes < $TESTDATA/pretty.txt | cmp $TESTDATA/binary - || fail parse pretty

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/pretty.txt - || fail decode
$CAPNP decode --flat $SCHEMA TestAllTypes < $TESTDATA/flat | cmp $TESTDATA/pretty.txt - || fail decode flat
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/packed | cmp $TESTDATA/pretty.txt - || fail decode packed
$CAPNP decode --packed --flat $SCHEMA TestAllTypes < $TESTDATA/packedflat | cmp $TESTDATA/pretty.txt - || fail decode packedflat
$CAPNP decode --short $SCHEMA TestAllTypes < $TESTDATA/binary | cmp $TESTDATA/short.txt - || fail decode short

$CAPNP decode $SCHEMA TestAllTypes < $TESTDATA/segmented | cmp $TESTDATA/pretty.txt - || fail decode segmented
$CAPNP decode --packed $SCHEMA TestAllTypes < $TESTDATA/segmented-packed | cmp $TESTDATA/pretty.txt - || fail decode segmented-packed

test_eval() {
  test "x`$CAPNP eval $SCHEMA $1 | tr -d '\r'`" = "x$2" || fail eval "$1 == $2"
}

test_eval TestDefaults.uInt32Field 3456789012
test_eval TestDefaults.structField.textField '"baz"'
test_eval TestDefaults.int8List "[111, -111]"
test_eval 'TestDefaults.structList[1].textField' '"structlist 2"'
test_eval globalPrintableStruct '(someText = "foo")'
test_eval TestConstants.enumConst corge
test_eval 'TestListDefaults.lists.int32ListList[2][0]' 12341234

$CAPNP compile -ofoo $TESTDATA/errors.capnp.nobuild 2>&1 | sed -e "s,^.*/errors[.]capnp[.]nobuild,file,g" | tr -d '\r' |
    cmp $TESTDATA/errors.txt - || fail error output
