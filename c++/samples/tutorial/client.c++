#include "pch.h"

int main()
{
	//std::cout << "starting" << std::endl;

	//kj::AsyncIoContext ioContext(kj::setupAsyncIo());
	//auto address = ioContext.provider->getNetwork().parseAddress("127.0.0.1", 2000).wait(ioContext.waitScope);

	// std::cout << "connecting" << std::endl;
	// auto connection = address->connect().wait(ioContext.waitScope);
	// capnp::TwoPartyClient client(*connection);

	// std::cout << "get root interface" << std::endl;
	// Root::Client root = client.bootstrap().castAs<Root>();
	// kj::WaitScope& wait = ioContext.waitScope;



	std::cout << "starting" << std::endl;
	capnp::EzRpcClient ezClient("127.0.0.1", 2000);
	kj::WaitScope& wait = ezClient.getWaitScope();

	std::cout << "get root interface" << std::endl;
	Root::Client root = ezClient.getMain<Root>();


	std::cout << "get sample interface" << std::endl;
	// getSample @0 () -> (v :Sample);
	Sample::Client sample = root.getSampleRequest().send().wait(wait).getV();

	std::cout << "simpleFunction()" << std::endl;
	// simpleFunction @0 ();
	sample.simpleFunctionRequest().send().wait(wait);

	{
		std::cout << "oneParam()" << std::endl;
		// oneParam @1 (variable :UInt16);
		auto var = sample.oneParamRequest();
		var.setVariable(5);
		var.send().wait(wait);
	}

	{
		std::cout << "responder() -> ";
		// responder @3 () -> (answer :UInt32);
		uint32_t answer = sample.responderRequest().send().wait(wait).getAnswer();
		std::cout << answer << std::endl;
	}

	{
		float input = 3.14;
		std::cout << "echo(" << input << ") -> ";
		// echo @4 (input :Float32) -> (response :Float32);
		auto var = sample.echoRequest();
		var.setInput(input);
		float output = var.send().wait(wait).getResponse();
		std::cout << output << std::endl;
	}

	{
		uint64_t v1 = 7;
		uint64_t v2 = 2;
		uint64_t v3 = 3;
		std::cout << "collect(" << v1 << ' ' << v2 << ' ' << v3  << ") -> ";
		// collect @5 (v1 :UInt64, v2 :UInt64, v3 :UInt64) -> (reply :List(UInt64));
		auto var = sample.collectRequest();
		var.setV1(v1);
		var.setV2(v2);
		var.setV3(v3);
		auto reply = var.send().wait(wait).getReply();
		std::cout << reply[0] << ' ' << reply[1] << ' ' << reply[2] << std::endl;
	}

	{
		uint64_t v1 = 7;
		uint64_t v2 = 2;
		uint64_t v3 = 3;
		std::cout << "separate(" << v1 << ' ' << v2 << ' ' << v3  << ") -> ";
		// separate @6 (param :List(UInt64)) -> (v1 :UInt64, v2 :UInt64, v3 :UInt64);
		auto var = sample.separateRequest();
		auto list = var.initParam(3);
		list.set(0, v1);
		list.set(1, v2);
		list.set(2, v3);
		auto reply = var.send().wait(wait);
		std::cout << reply.getV1() << ' ' << reply.getV2() << ' ' << reply.getV3() << std::endl;
	}

	{
		std::cout << "setState(true)" << std::endl;
		// setState @7 (happy :Bool);
		auto var = sample.setStateRequest();
		var.setHappy(true);
		var.send().wait(wait);
	}

	{
		std::cout << "getState() -> ";
		// getState @8 () -> (happy :Bool);
		bool happy = sample.getStateRequest().send().wait(wait).getHappy();
		std::cout << happy << std::endl;
	}



	std::cout << "done" << std::endl;
}