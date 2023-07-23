@0xd04299800d6725ba;

$import "/capnp/c++.capnp".namespace("capnp::json");

using Json = import "json.capnp";

struct RpcMessage {
  jsonrpc @0 :Text;
  # Must always be "2.0".

  id @1 :Json.Value;
  # Correlates a request to a response. Technically must be a string or number. Our implementation
  # will always use a number for calls it initiates, and will reflect IDs of any type for calls
  # it receives.
  #
  # May be omitted when caller doesn't care about the response. The implementation will omit `id`
  # and return immediately when calling methods with the annotation `@notification` (defined in
  # `json.capnp`). The `@notification` annotation only matters for outgoing calls; for incoming
  # calls, it's the client's decision whether it wants to receive the response.

  method @2 :Text;
  # Method name. Only expected when `params` is sent.

  union {
    none @3 :Void $Json.name("!missing params, result, or error");
    # Dummy default value of union, to detect when none of the fields below were received.

    params @4 :Json.Value;
    # Initiates a call.

    result @5 :Json.Value;
    # Completes a call.

    error @6 :Error;
    # Completes a call throwing an exception.
  }

  struct Error {
    code @0 :Int32;
    message @1 :Text;
    data @2 :Json.Value;
  }
}
