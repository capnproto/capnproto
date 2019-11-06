#include "pch.h"
#include "secure.h"

kj::Own<kj::PromiseFulfiller<void>> g_terminate;

// shutdownService @0 ();
kj::Promise<void> CSecure::shutdownService(ShutdownServiceContext context)
{
	g_terminate->fulfill();
	return kj::READY_NOW;
}

void sig_handler(int sig)
{
	g_terminate->fulfill();
}

void signalInit()
{
	signal(SIGINT, sig_handler);
}