@0xe61e5a477b5066d9;

interface Root
{
	getSample @0 () -> (v :Sample);
}

interface Sample
{
	simpleFunction @0 ();
	oneParam @1 (variable :UInt16);
	notImpl @2 ();
	responder @3 () -> (answer :UInt32);
	echo @4 (input :Float32) -> (response :Float32);
	collect @5 (v1 :UInt64, v2 :UInt64, v3 :UInt64) -> (reply :List(UInt64));
	separate @6 (param :List(UInt64)) -> (v1 :UInt64, v2 :UInt64, v3 :UInt64);
	setState @7 (happy :Bool);
	getState @8 () -> (happy :Bool);
}