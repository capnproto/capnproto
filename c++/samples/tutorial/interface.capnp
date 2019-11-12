@0xe61e5a477b5066d9;

# This is the main interface, any other interfaces must come from here directly or indirectly.
interface Root
{
	getSample @0 () -> (v :Sample);
	getSecure @1 (password :UInt64) -> (v :Secure);
}

interface Sample
{
	simpleFunction @0 ();
	oneParam @1 (variable :UInt16);
	notImpl @2 ();	# This function is not implemented. It's here because something has to be @2. This is a common problem.
	responder @3 () -> (answer :UInt32);
	echo @4 (input :Float32) -> (response :Float32);
	collect @5 (v1 :UInt64, v2 :UInt64, v3 :UInt64) -> (reply :List(UInt64));
	separate @6 (param :List(UInt64)) -> (v1 :UInt64, v2 :UInt64, v3 :UInt64);
	setState @7 (happy :Bool);
	getState @8 () -> (happy :Bool);
	echoStruct @9 (str :ExampleStruct) -> (str :ExampleStruct);
	echoData @10 (d :Data) -> (d :Data);

	struct ExampleStruct
	{
		a @0 :UInt32;
		b @1 :Text;
	}
}

interface Secure
{
	shutdownService @0 ();
}