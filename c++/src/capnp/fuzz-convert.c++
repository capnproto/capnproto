#include <fcntl.h>

#include <kj/io.h>
#include <kj/main.h>
#include "message.h"
#include "serialize.h"
#include "serialize-packed.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    kj::ArrayPtr<const uint8_t> array(Data, Size);
    kj::ArrayInputStream ais(array);

    capnp::ReaderOptions options;

    //TODO use readOneAndConvert(input, output) ?
    //with int fd = open("/dev/null", O_WRONLY);
    //and kj::FdOutputStream output(fd);

    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
        kj::BufferedInputStreamWrapper input(ais);
        capnp::InputStreamMessageReader message(input, options);
        capnp::PackedMessageReader messagep(input, options);
    })) {
        KJ_LOG(ERROR, "threw");
    }

    return 0;
}
