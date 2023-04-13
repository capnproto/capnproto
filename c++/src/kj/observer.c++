#include "observer.h"
#include <boost/signals2.hpp> // keep this out of exposed headers

namespace kj {

class SubscriptionBaseImpl : public _::SubscriptionBase {
public:
  explicit SubscriptionBaseImpl(boost::signals2::connection&& connection) : connection(mv(connection)) {}
  boost::signals2::scoped_connection connection;
};

class VoidSubjectBaseImpl : public _::VoidSubjectBase {
public:
  Own<_::SubscriptionBase> subscribe(Function<void()> &&callback) override {
    auto cb = heap<Function<void()>>(mv(callback));
    return heap<SubscriptionBaseImpl>(signal.connect([&callback = *cb]() {
      callback();
    })).attach(mv(cb));
  }

  void notify() override {
    signal();
  }

private:
  boost::signals2::signal<void()> signal;
};

class ParameterSubjectBaseImpl : public _::ParameterSubjectBase {
public:
  Own<_::SubscriptionBase> subscribe(Function<void(const void*)>&& callback) override {
    auto cb = heap<Function<void(const void*)>>(mv(callback));
    return heap<SubscriptionBaseImpl>(signal.connect([&callback = *cb](const void* event) {
      callback(event);
    })).attach(mv(cb));
  }

  void notify(const void* event) override {
    signal(event);
  }

private:
  boost::signals2::signal<void(const void*)> signal;
};

namespace _ {
Own<_::VoidSubjectBase> newVoidSubjectBase() {
  return heap<VoidSubjectBaseImpl>();
}

Own<_::ParameterSubjectBase> newParameterSubjectBase() {
  return heap<ParameterSubjectBaseImpl>();
}
}

Subscription::Subscription(Own<_::SubscriptionBase> &&base) : base(mv(base)) {}

}
