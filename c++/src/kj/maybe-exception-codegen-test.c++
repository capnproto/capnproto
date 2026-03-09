#include <kj/async.h>
#include <kj/test.h>

namespace kj {
namespace {

struct Dep {
  virtual void get(kj::_::ExceptionOrValue& output) = 0;
};

template <typename T>
struct DepHolder {
  kj::Own<T> dependency;
  void* continuationTracePtr = nullptr;

  // TransformPromiseNodeBase::getDepResult
  KJ_NOINLINE void getDepResult(kj::_::ExceptionOrValue& output) {
    dependency->get(output);
    KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
      dependency = nullptr;
    })) {
      output.addException(kj::mv(exception));
    }

    KJ_IF_SOME(e, output.exception) {
      e.addTrace(continuationTracePtr);
    }
  }
};

struct FortyTwo final: public Dep {
  void get(kj::_::ExceptionOrValue& output) override {
    output.as<int>().value = 42;
  }
};

KJ_TEST("Maybe<Exception> codegen - getDepResult") {
  DepHolder<Dep> holder { .dependency = kj::heap<FortyTwo>() };
  kj::_::ExceptionOr<int> output;
  holder.getDepResult(output);
  KJ_ASSERT(output.value == 42);
  KJ_ASSERT(output.exception == kj::none);
}

}  // namespace
}  // namespace kj
