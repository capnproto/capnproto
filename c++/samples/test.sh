#! /bin/bash
#
# Quick script that compiles and runs the samples, then cleans up.
# Used for release testing.

set -exuo pipefail

capnpc -oc++ addressbook.capnp
c++ -std=c++11 -Wall addressbook.c++ addressbook.capnp.c++ -lcapnp -lkj -pthread -o addressbook
./addressbook write | ./addressbook read
./addressbook dwrite | ./addressbook dread
rm addressbook addressbook.capnp.c++ addressbook.capnp.h

capnpc -oc++ calculator.capnp
c++ -std=c++11 -Wall calculator-client.c++ calculator.capnp.c++ -lcapnp-rpc -lcapnp -lkj-async \
    -lkj -pthread -o calculator-client
c++ -std=c++11 -Wall calculator-server.c++ calculator.capnp.c++ -lcapnp-rpc -lcapnp -lkj-async \
    -lkj -pthread -o calculator-server
rm -f /tmp/capnp-calculator-example-$$
./calculator-server unix:/tmp/capnp-calculator-example-$$ &
sleep 0.1
./calculator-client unix:/tmp/capnp-calculator-example-$$
kill %+
wait %+ || true
rm calculator-client calculator-server calculator.capnp.c++ calculator.capnp.h /tmp/capnp-calculator-example-$$
