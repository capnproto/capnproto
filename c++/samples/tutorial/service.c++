#include "pch.h"


struct CSample final: public Sample::Server
{
	// simpleFunction @0 ();
	kj::Promise<void> simpleFunction(SimpleFunctionContext context)
	{
		doSomething();
		return kj::READY_NOW;
	}

	// oneParam @1 (variable :UInt16);
	kj::Promise<void> oneParam(OneParamContext context)
	{
		unsigned short variable = context.getParams().getVariable();
		doSomething();
		return kj::READY_NOW;
	}

	// notImpl @2 ();
	// not implemented, because I didn't want to renumber or reuse @2 in the .capnp file.
	// the caller will get an exception if they try to call this function

	// responder @3 () -> (answer :UInt32);
	kj::Promise<void> responder(ResponderContext context)
	{
		doSomething();
		context.getResults().setAnswer(42);
		return kj::READY_NOW;
	}

	// echo @4 (input :Float32) -> (response :Float32);
	kj::Promise<void> echo(EchoContext context)
	{
		float variable = context.getParams().getInput();
		context.getResults().setResponse(variable);
		return kj::READY_NOW;
	}

	// collect @5 (v1 :UInt64, v2 :UInt64, v3 :UInt64) -> (reply :List(UInt64));
	kj::Promise<void> collect(CollectContext context)
	{
		uint64_t v1 = context.getParams().getV1();
		uint64_t v2 = context.getParams().getV2();
		uint64_t v3 = context.getParams().getV3();

		auto list = context.getResults().initReply(3);
		list.set(0, v1);
		list.set(1, v2);
		list.set(2, v3);
		return kj::READY_NOW;
	}
	
	// separate @6 (param :List(UInt64)) -> (v1 :UInt64, v2 :UInt64, v3 :UInt64);
	kj::Promise<void> separate(SeparateContext context)
	{
		auto list = context.getParams().getParam();
		if (list.size() != 3)
			KJ_FAIL_REQUIRE("separate() list must have exactly 3 numbers");
		context.getResults().setV1(list[0]);
		context.getResults().setV2(list[1]);
		context.getResults().setV3(list[2]);
		return kj::READY_NOW;
	}
	
	// setState @7 (happy :Bool);
	kj::Promise<void> setState(SetStateContext context)
	{
		happy = context.getParams().getHappy();
		return kj::READY_NOW;
	}

	// getState @8 () -> (happy :Bool);
	kj::Promise<void> getState(GetStateContext context)
	{
		context.getResults().setHappy(happy);
		return kj::READY_NOW;
	}

	bool happy = false;
	void doSomething(){}	// stub function, fill in however you like
};

struct CRoot final: public Root::Server
{
	// getSample @0 () -> (v :Sample);
	kj::Promise<void> getSample(GetSampleContext context)
	{
		context.getResults().setV(kj::heap<CSample>());
		return kj::READY_NOW;
	}
};

int main()
{
	kj::AsyncIoContext ioContext(kj::setupAsyncIo());
	capnp::TwoPartyServer server(kj::heap<CRoot>());
	auto address = ioContext.provider->getNetwork().parseAddress("127.0.0.1", 2000).wait(ioContext.waitScope);
	auto listener = address->listen();
	auto listenPromise = server.listen(*listener);
	listenPromise.wait(ioContext.waitScope);
}