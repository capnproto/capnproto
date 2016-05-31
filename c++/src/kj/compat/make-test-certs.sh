#! /bin/bash
# Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

# This script generates the test keys and certificates used in tls-test.c++.

set -euxo pipefail

mkdir -p tmp/test-certs
cd tmp/test-certs

# Clean up from previous runs.
rm -rf demoCA *.key *.csr *.crt

# Function to fake out OpenSSL CA configuration. Pass base name of files as parameter.
setup_ca_dir() {
  rm -rf demoCA
  mkdir -p demoCA/private demoCA/newcerts
  ln -s ../../$1.key demoCA/private/cakey.pem
  ln -s ../$1.crt demoCA/cacert.pem
  touch demoCA/index.txt
  echo 1000 > demoCA/serial
}

# Create CA key and root cert
openssl genrsa -out ca.key 4096
openssl req -key ca.key -new -x509 -days 36500 -sha256 -extensions v3_ca -out ca.crt << EOF
US
California
Palo Alto
Sandstorm.io
Testing Department
ca.example.com
garply@sandstorm.io
EOF
echo

# Create intermediate certificate and CSR.
openssl genrsa -out int.key 4096
openssl req -new -sha256 -key int.key -out int.csr << EOF
US
California
Palo Alto
Sandstorm.io
Testing Department
int-ca.example.com
garply@sandstorm.io


EOF
echo

# Sign the intermediate cert with the CA key.
setup_ca_dir ca
openssl ca -extensions v3_ca -days 36500 -notext -md sha256 -in int.csr -out int.crt << EOF
y
y
EOF
cat ca.crt int.crt > ca-chain.crt

# Create host key and CSR
openssl genrsa -out example.key 4096
openssl req -new -sha256 -key example.key -out example.csr << EOF
US
California
Palo Alto
Sandstorm.io
Testing Department
example.com
garply@sandstorm.io


EOF
echo

# Sign valid host certificate with intermediate CA.
setup_ca_dir int
openssl ca -extensions v3_ca -days 36524 -notext -md sha256 -in example.csr -out valid.crt << EOF
y
y
EOF

# Sign expired host certificate with intermediate CA.
setup_ca_dir int
openssl ca -extensions v3_ca -startdate 160101000000Z -enddate 160101000000Z -notext -md sha256 -in example.csr -out expired.crt << EOF
y
y
EOF

# Create self-signed host certificate.
openssl req -key example.key -new -x509 -days 36524 -sha256 -out self.crt << EOF
US
California
Palo Alto
Sandstorm.io
Testing Department
example.com
garply@sandstorm.io
EOF
echo

# Cleanup
rm -rf demoCA

# Output code.
write_constant() {
  echo "static constexpr char $1[] ="
  sed -e 's/^.*$/    "\0\\n"/g;s/--END .*$/\0;/g' $2
  echo
}

echo "Writing code to: tmp/test-certs/test-keys.h"

exec 1> test-keys.h
write_constant CA_CERT ca.crt
write_constant INTERMEDIATE_CERT int.crt
write_constant HOST_KEY example.key
write_constant VALID_CERT valid.crt
write_constant EXPIRED_CERT expired.crt
write_constant SELF_SIGNED_CERT self.crt
