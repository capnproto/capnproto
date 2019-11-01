#include "pch.h"

int main()
{
	std::cout << "starting" << std::endl;

	kj::AsyncIoContext ioContext(kj::setupAsyncIo());
	auto address = ioContext.provider->getNetwork().parseAddress("127.0.0.1", 2000).wait(ioContext.waitScope);

	std::cout << "connecting" << std::endl;
	auto connection = address->connect().wait(ioContext.waitScope);
	capnp::TwoPartyClient client(*connection);

	std::cout << "get root interface" << std::endl;
	Root::Client root = client.bootstrap().castAs<Root>();
	kj::WaitScope& wait = ioContext.waitScope;

	std::cout << "get sample interface" << std::endl;
	// getSample @0 () -> (v :Sample);
	Sample::Client sample = root.getSampleRequest().send().wait(wait).getV();

	std::cout << "simpleFunction()" << std::endl;
	// simpleFunction @0 ();
	sample.simpleFunctionRequest().send().wait(wait);

	// oneParam @1 (variable :UInt16);
	{
		std::cout << "oneParam()" << std::endl;
		auto var = sample.oneParamRequest();
		var.setVariable(5);
		var.send().wait(wait);
	}

	// responder @3 () -> (answer :UInt32);
	{
		std::cout << "responder() -> ";
		uint32_t answer = sample.responderRequest().send().wait(wait).getAnswer();
		std::cout << answer << std::endl;
	}

	// echo @4 (input :Float32) -> (response :Float32);
	{
		float input = 3.14;
		std::cout << "echo(" << input << ") -> ";
		auto var = sample.echoRequest();
		var.setInput(input);
		float output = var.send().wait(wait).getResponse();
		std::cout << output << std::endl;
	}

	// collect @5 (v1 :UInt64, v2 :UInt64, v3 :UInt64) -> (reply :List(UInt64));
	{
		uint64_t v1 = 7;
		uint64_t v2 = 2;
		uint64_t v3 = 3;
		std::cout << "collect(" << v1 << ' ' << v2 << ' ' << v3  << ") -> ";
		auto var = sample.collectRequest();
		var.setV1(v1);
		var.setV2(v2);
		var.setV3(v3);
		auto reply = var.send().wait(wait).getReply();
		std::cout << reply[0] << ' ' << reply[1] << ' ' << reply[2] << std::endl;
	}

	// separate @6 (param :List(UInt64)) -> (v1 :UInt64, v2 :UInt64, v3 :UInt64);
	{
		uint64_t v1 = 7;
		uint64_t v2 = 2;
		uint64_t v3 = 3;
		std::cout << "separate(" << v1 << ' ' << v2 << ' ' << v3  << ") -> ";
		auto var = sample.separateRequest();
		auto list = var.initParam(3);
		list.set(0, v1);
		list.set(1, v2);
		list.set(2, v3);
		auto reply = var.send().wait(wait);
		std::cout << reply.getV1() << ' ' << reply.getV2() << ' ' << reply.getV3() << std::endl;
	}

	// setState @7 (happy :Bool);
	{
		std::cout << "setState(true)" << std::endl;
		auto var = sample.setStateRequest();
		var.setHappy(true);
		var.send().wait(wait);
	}

	// getState @8 () -> (happy :Bool);
	{
		std::cout << "getState() -> ";
		bool happy = sample.getStateRequest().send().wait(wait).getHappy();
		std::cout << happy << std::endl;
	}

	{
		// getSecure @1 (password :UInt64) -> (v :Secure);
		std::cout << "get secure interface using secret password" << std::endl;
		auto var = root.getSecureRequest();
		var.setPassword(42);
		Secure::Client secure = var.send().wait(wait).getV();

		// shutdownService @0 ();
		std::cout << "ShutdownServiceRequest()" << std::endl;
		secure.shutdownServiceRequest().send().wait(wait);
	}

	std::cout << "done" << std::endl;
}