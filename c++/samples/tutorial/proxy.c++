#include "pch.h"
#include "secure.h"

// This is a proxy service that passes along capabilities (interfaces) and forwards all messages
// You don't need to use this, the client and service will work without the proxy

extern kj::Own<kj::PromiseFulfiller<void>> g_terminate;

namespace
{
	struct Network
	{
		Network()
		: context(kj::setupAsyncIo())
		, addressListen( context.provider->getNetwork().parseAddress("127.0.0.1", 2000).wait(context.waitScope))
		, addressConnect(context.provider->getNetwork().parseAddress("127.0.0.1", 2001).wait(context.waitScope))
		{}
		kj::AsyncIoContext context;
		kj::Own<kj::NetworkAddress> addressListen;
		kj::Own<kj::NetworkAddress> addressConnect;
	};

	Network network;
	struct Client
	{
		Client()
		: connection(network.addressConnect->connect().wait(network.context.waitScope))
		, client(*connection)
		, root(client.bootstrap().castAs<Root>())
		{
		}

		kj::Own<kj::AsyncIoStream> connection;
		capnp::TwoPartyClient client;
		Root::Client root;
	};

	Client client;
}


struct CRoot final: public Root::Server
{
	bool shutdown = false;

	template <typename TC, typename TR, bool terminate = false>
	struct ThenPassInterface
	{
		ThenPassInterface(TC& context)
		: context(context)
		{}

		void operator()(capnp::Response<TR> response)
		{
			context.getResults().setV(response.getV());
		}
		TC context;
	};

	struct ErrorHandler
	{
		void operator()(kj::Exception e)
		{
			std::cout << "ErrorHandler " << e.getDescription().cStr() << std::endl;
			g_terminate->fulfill();
		}
	};

	// getSample @0 () -> (v :Sample);
	kj::Promise<void> getSample(GetSampleContext context)
	{
		return client.root.getSampleRequest().send().then(ThenPassInterface<GetSampleContext, GetSampleResults>(context), ErrorHandler());
	}

	// getSecure @1 (password :UInt64) -> (v :Secure);
	kj::Promise<void> getSecure(GetSecureContext context)
	{
		auto request = client.root.getSecureRequest();
		request.setPassword(context.getParams().getPassword());
		auto promise = request.send().then(ThenPassInterface<GetSecureContext, GetSecureResults, true>(context), ErrorHandler());
		return promise.eagerlyEvaluate(ErrorHandler());
	}
};

int main()
{
	capnp::TwoPartyServer server(kj::heap<CRoot>());
	auto listener = network.addressListen->listen();
	auto listenPromise = server.listen(*listener);

	kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>();
	g_terminate = kj::mv(paf.fulfiller);

	network.context.unixEventPort.onSignal(SIGINT).ignoreResult().exclusiveJoin(kj::mv(paf.promise)).wait(network.context.waitScope);
}