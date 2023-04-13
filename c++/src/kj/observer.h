#pragma once

#include "common.h"
#include "function.h"
#include "memory.h"

KJ_BEGIN_HEADER

namespace kj {

namespace _ {

class SubscriptionBase {
public:
  virtual ~SubscriptionBase() = default;
};

class VoidSubjectBase {
public:
  virtual Own<SubscriptionBase> subscribe(Function<void()> &&callback) = 0;

  virtual void notify() = 0;

  virtual ~VoidSubjectBase() = default;
};

Own<VoidSubjectBase> newVoidSubjectBase();

class ParameterSubjectBase {
public:
  virtual Own<SubscriptionBase> subscribe(Function<void(const void *event)> &&callback) = 0;

  virtual void notify(const void *event) = 0;

  virtual ~ParameterSubjectBase() = default;
};

Own<ParameterSubjectBase> newParameterSubjectBase();

}

class Subscription {
public:
  explicit Subscription(Own<_::SubscriptionBase>&& base);
  Subscription(Subscription&&) = default;
private:
  Own<_::SubscriptionBase> base;
};

template <typename T>
class Subject
{
public:
  Subject() : base(_::newParameterSubjectBase()) {}
  Subscription subscribe(Function<void(const T&)>&& callback) {
    return Subscription(base->subscribe([callback = mv(callback)](const void* event) mutable {
      callback(*static_cast<const T*>(event));
    }));
  }

  void notify(const T& event) {
    base->notify(static_cast<const void*>(&event));
  }

private:
  Own<_::ParameterSubjectBase> base;
};

template <>
class Subject<void>
{
public:
  Subject() : base(_::newVoidSubjectBase()) {}

  Subscription subscribe(Function<void()>&& callback) {
    return Subscription(base->subscribe(mv(callback)));
  }

  void notify() {
    base->notify();
  }

private:
  Own<_::VoidSubjectBase> base;
};

}

KJ_END_HEADER
