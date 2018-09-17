# Copyright (c) 2018 Cloudflare, Inc. and contributors
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

@0xc9d405cf4333e4c9;

using Json = import "/capnp/compat/json.capnp";

$import "/capnp/c++.capnp".namespace("capnp");

struct TestJsonAnnotations {
  someField @0 :Text $Json.name("names-can_contain!anything Really");

  aGroup :group $Json.flatten() {
    flatFoo @1 :UInt32;
    flatBar @2 :Text;
    flatBaz :group $Json.name("renamed-flatBaz") {
      hello @3 :Bool;
    }
    doubleFlat :group $Json.flatten() {
      flatQux @4 :Text;
    }
  }

  prefixedGroup :group $Json.flatten(prefix = "pfx.") {
    foo @5 :Text;
    bar @6 :UInt32 $Json.name("renamed-bar");
    baz :group {
      hello @7 :Bool;
    }
    morePrefix :group $Json.flatten(prefix = "xfp.") {
      qux @8 :Text;
    }
  }

  aUnion :union $Json.flatten() $Json.discriminator(name = "union-type") {
    foo :group $Json.flatten() {
      fooMember @9 :Text;
      multiMember @10 :UInt32;
    }
    bar :group $Json.flatten() $Json.name("renamed-bar") {
      barMember @11 :UInt32;
      multiMember @12 :Text;
    }
  }

  dependency @13 :TestJsonAnnotations2;
  # To test that dependencies are loaded even if not flattened.

  simpleGroup :group {
    # To test that group types are loaded even if not flattened.
    grault @14 :Text $Json.name("renamed-grault");
  }

  enums @15 :List(TestJsonAnnotatedEnum);

  innerJson @16 :Json.Value;

  customFieldHandler @17 :Text;

  testBase64 @18 :Data $Json.base64;
  testHex @19 :Data $Json.hex;

  bUnion :union $Json.flatten() $Json.discriminator(valueName = "bValue") {
    foo @20 :Text;
    bar @21 :UInt32 $Json.name("renamed-bar");
  }

  externalUnion @22 :TestJsonAnnotations3;

  unionWithVoid :union $Json.discriminator(name = "type") {
    intValue @23 :UInt32;
    voidValue @24 :Void;
    textValue @25 :Text;
  }
}

struct TestJsonAnnotations2 {
  foo @0 :Text $Json.name("renamed-foo");
  cycle @1 :TestJsonAnnotations;
}

struct TestJsonAnnotations3 $Json.discriminator(name = "type") {
  union {
    foo @0 :UInt32;
    bar @1 :TestFlattenedStruct $Json.flatten();
  }
}

struct TestFlattenedStruct {
  value @0 :Text;
}

enum TestJsonAnnotatedEnum {
  foo @0;
  bar @1 $Json.name("renamed-bar");
  baz @2 $Json.name("renamed-baz");
  qux @3;
}

struct TestBase64Union {
  union {
    foo @0 :Data $Json.base64;
    bar @1 :Text;
  }
}
