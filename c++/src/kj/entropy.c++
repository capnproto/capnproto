#include <syscall.h>
#include <fcntl.h>
#include <unistd.h>

#include "debug.h"
#include "entropy.h"

namespace kj {
namespace {

#if __linux__
void getSecureRandom(kj::byte* buffer, size_t length) {
  // Note: The glibc wrapper for getrandom() appeared long after the syscall landed in the kernel,
  //   hence we need to use syscall() here.
  int n;
  KJ_SYSCALL(n = syscall(SYS_getrandom, buffer, length, 0));
  if (n < length) {
    getSecureRandom(buffer + n, length - n);
  }
}
#else
void getSecureRandom(kj::byte* buffer, size_t length) {
  KJ_UNIMPLEMENTED("getSecureRandom() not implemented on this platform");
}
#endif

class EntropySourceImpl: public EntropySource {
public:
  void generate(kj::ArrayPtr<byte> buffer) override {
    getSecureRandom(buffer.begin(), buffer.size());
  }

  static EntropySourceImpl instance;
};

EntropySourceImpl EntropySourceImpl::instance;

}  // namespace

EntropySource& systemCsprng() {
  return EntropySourceImpl::instance;
}

}  // namespace kj
