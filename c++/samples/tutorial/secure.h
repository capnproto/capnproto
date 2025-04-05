#pragma once
#include "pch.h"

struct CSecure final: public Secure::Server
{
	// shutdownService @0 ();
	kj::Promise<void> shutdownService(ShutdownServiceContext context);
};

void signalInit();