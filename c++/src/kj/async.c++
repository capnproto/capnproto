// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "async.h"
#include "debug.h"
#include "vector.h"
#include <exception>
#include <map>

#if KJ_USE_FUTEX
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#endif

namespace kj {

namespace {

static __thread EventLoop* threadLocalEventLoop = nullptr;

#define _kJ_ALREADY_READY reinterpret_cast< ::kj::EventLoop::Event*>(1)

class BoolEvent: public EventLoop::Event {
public:
  BoolEvent(const EventLoop& loop): Event(loop) {}
  ~BoolEvent() { disarm(); }

  bool fired = false;

  void fire() override {
    fired = true;
  }
};

class YieldPromiseNode final: public _::PromiseNode {
public:
  bool onReady(EventLoop::Event& event) noexcept override {
    event.arm(false);
    return false;
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    output.as<_::Void>().value = _::Void();
  }
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override {
    return nullptr;
  }
};

}  // namespace

EventLoop& EventLoop::current() {
  EventLoop* result = threadLocalEventLoop;
  KJ_REQUIRE(result != nullptr, "No event loop is running on this thread.");
  return *result;
}

bool EventLoop::isCurrent() const {
  return threadLocalEventLoop == this;
}

EventLoop::EventLoop() {}

void EventLoop::waitImpl(Own<_::PromiseNode> node, _::ExceptionOrValue& result) {
  EventLoop* oldEventLoop = threadLocalEventLoop;
  threadLocalEventLoop = this;
  KJ_DEFER(threadLocalEventLoop = oldEventLoop);

  BoolEvent event(*this);
  event.fired = node->onReady(event);

  while (!event.fired) {
    KJ_IF_MAYBE(event, queue.peek(nullptr)) {
      // Arrange for events armed during the event callback to be inserted at the beginning
      // of the queue.
      insertionPoint = nullptr;

      // Fire the first event.
      event->complete(0);
    } else {
      // No events in the queue.  Wait for callback.
      prepareToSleep();
      if (queue.peek(*this) != nullptr) {
        // Whoa, new job was just added.
        wake();
      }
      sleep();
    }
  }

  node->get(result);
}

Promise<void> EventLoop::yieldIfSameThread() const {
  return Promise<void>(false, kj::heap<YieldPromiseNode>());
}

void EventLoop::receivedNewJob() const {
  wake();
}

EventLoop::Event::Event(const EventLoop& loop)
    : loop(loop),
      jobs { loop.queue.createJob(*this), loop.queue.createJob(*this) } {}

EventLoop::Event::~Event() noexcept(false) {}

void EventLoop::Event::arm(bool preemptIfSameThread) {
  EventLoop* localLoop = threadLocalEventLoop;
  if (preemptIfSameThread && localLoop == &loop) {
    // Insert the event into the queue.  We put it at the front rather than the back so that
    // related events are executed together and so that increasing the granularity of events
    // does not cause your code to "lose priority" compared to simultaneously-running code
    // with less granularity.
    jobs[currentJob]->insertAfter(localLoop->insertionPoint, localLoop->queue);
    localLoop->insertionPoint = *jobs[currentJob];
  } else {
    // Insert the node at the *end* of the queue.
    jobs[currentJob]->addToQueue();
  }
  currentJob = !currentJob;
}

void EventLoop::Event::disarm() {
  jobs[0]->cancel();
  jobs[1]->cancel();
}

// =======================================================================================

#if KJ_USE_FUTEX

SimpleEventLoop::SimpleEventLoop() {}
SimpleEventLoop::~SimpleEventLoop() noexcept(false) {}

void SimpleEventLoop::prepareToSleep() noexcept {
  __atomic_store_n(&preparedToSleep, 1, __ATOMIC_RELAXED);
}

void SimpleEventLoop::sleep() {
  while (__atomic_load_n(&preparedToSleep, __ATOMIC_RELAXED) == 1) {
    syscall(SYS_futex, &preparedToSleep, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0);
  }
}

void SimpleEventLoop::wake() const {
  if (__atomic_exchange_n(&preparedToSleep, 0, __ATOMIC_RELAXED) != 0) {
    // preparedToSleep was 1 before the exchange, so a sleep must be in progress in another thread.
    syscall(SYS_futex, &preparedToSleep, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  }
}

#else

#define KJ_PTHREAD_CALL(code) \
  { \
    int pthreadError = code; \
    if (pthreadError != 0) { \
      KJ_FAIL_SYSCALL(#code, pthreadError); \
    } \
  }

#define KJ_PTHREAD_CLEANUP(code) \
  { \
    int pthreadError = code; \
    if (pthreadError != 0) { \
      KJ_LOG(ERROR, #code, strerror(pthreadError)); \
    } \
  }

SimpleEventLoop::SimpleEventLoop() {
  KJ_PTHREAD_CALL(pthread_mutex_init(&mutex, nullptr));
  KJ_PTHREAD_CALL(pthread_cond_init(&condvar, nullptr));
}
SimpleEventLoop::~SimpleEventLoop() noexcept(false) {
  KJ_PTHREAD_CLEANUP(pthread_cond_destroy(&condvar));
  KJ_PTHREAD_CLEANUP(pthread_mutex_destroy(&mutex));
}

void SimpleEventLoop::prepareToSleep() noexcept {
  pthread_mutex_lock(&mutex);
  preparedToSleep = 1;
}

void SimpleEventLoop::sleep() {
  while (preparedToSleep == 1) {
    pthread_cond_wait(&condvar, &mutex);
  }
  pthread_mutex_unlock(&mutex);
}

void SimpleEventLoop::wake() const {
  pthread_mutex_lock(&mutex);
  if (preparedToSleep != 0) {
    // preparedToSleep was 1 before the exchange, so a sleep must be in progress in another thread.
    preparedToSleep = 0;
    pthread_cond_signal(&condvar);
  }
  pthread_mutex_unlock(&mutex);
}

#endif

// =======================================================================================

void PromiseBase::absolve() {
  runCatchingExceptions([this]() { node = nullptr; });
}

class TaskSet::Impl {
public:
  inline Impl(const EventLoop& loop, ErrorHandler& errorHandler)
    : loop(loop), errorHandler(errorHandler) {}

  ~Impl() noexcept(false) {
    // std::map doesn't like it when elements' destructors throw, so carefully disassemble it.
    auto& taskMap = tasks.getWithoutLock();
    if (!taskMap.empty()) {
      Vector<Own<Task>> deleteMe(taskMap.size());
      for (auto& entry: taskMap) {
        deleteMe.add(kj::mv(entry.second));
      }
    }
  }

  class Task final: public EventLoop::Event {
  public:
    Task(const Impl& taskSet, Own<_::PromiseNode>&& nodeParam)
        : EventLoop::Event(taskSet.loop), taskSet(taskSet), node(kj::mv(nodeParam)) {
      if (node->onReady(*this)) {
        arm();
      }
    }

    ~Task() {
      disarm();
    }

  protected:
    void fire() override {
      // Get the result.
      _::ExceptionOr<_::Void> result;
      node->get(result);

      // Delete the node, catching any exceptions.
      KJ_IF_MAYBE(exception, runCatchingExceptions([this]() {
        node = nullptr;
      })) {
        result.addException(kj::mv(*exception));
      }

      // Call the error handler if there was an exception.
      KJ_IF_MAYBE(e, result.exception) {
        taskSet.errorHandler.taskFailed(kj::mv(*e));
      }
    }

  private:
    const Impl& taskSet;
    kj::Own<_::PromiseNode> node;
  };

  void add(Promise<void>&& promise) const {
    auto task = heap<Task>(*this, _::makeSafeForLoop<_::Void>(kj::mv(promise.node), loop));
    Task* ptr = task;
    tasks.lockExclusive()->insert(std::make_pair(ptr, kj::mv(task)));
  }

private:
  const EventLoop& loop;
  ErrorHandler& errorHandler;

  // TODO(soon):  Use a linked list instead.  We should factor out the intrusive linked list code
  //   that appears in EventLoop and ForkHub.
  MutexGuarded<std::map<Task*, Own<Task>>> tasks;
};

TaskSet::TaskSet(const EventLoop& loop, ErrorHandler& errorHandler)
    : impl(heap<Impl>(loop, errorHandler)) {}

TaskSet::~TaskSet() noexcept(false) {}

void TaskSet::add(Promise<void>&& promise) const {
  impl->add(kj::mv(promise));
}

namespace _ {  // private

bool PromiseNode::atomicOnReady(EventLoop::Event*& onReadyEvent, EventLoop::Event& newEvent) {
  // If onReadyEvent is null, atomically set it to point at newEvent and return false.
  // If onReadyEvent is _kJ_ALREADY_READY, return true.
  // Useful for implementing onReady() thread-safely.

  EventLoop::Event* oldEvent = nullptr;
  if (__atomic_compare_exchange_n(&onReadyEvent, &oldEvent, &newEvent, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    // Event was swapped in and will be called later.
    return false;
  } else {
    // `onReadyEvent` is not null.  If it is _kJ_ALREADY_READY then this promise was fulfilled
    // before any dependent existed, otherwise there is already a different dependent.
    KJ_REQUIRE(oldEvent == _kJ_ALREADY_READY, "onReady() can only be called once.");
    return true;
  }
}

void PromiseNode::atomicReady(EventLoop::Event*& onReadyEvent) {
  // If onReadyEvent is null, atomically set it to _kJ_ALREADY_READY.
  // Otherwise, arm whatever it points at.
  // Useful for firing events in conjuction with atomicOnReady().

  EventLoop::Event* oldEvent = nullptr;
  if (!__atomic_compare_exchange_n(&onReadyEvent, &oldEvent, _kJ_ALREADY_READY, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    oldEvent->arm();
  }
}

// -------------------------------------------------------------------

bool ImmediatePromiseNodeBase::onReady(EventLoop::Event& event) noexcept { return true; }
Maybe<const EventLoop&> ImmediatePromiseNodeBase::getSafeEventLoop() noexcept { return nullptr; }

ImmediateBrokenPromiseNode::ImmediateBrokenPromiseNode(Exception&& exception)
    : exception(kj::mv(exception)) {}

void ImmediateBrokenPromiseNode::get(ExceptionOrValue& output) noexcept {
  output.exception = kj::mv(exception);
}

// -------------------------------------------------------------------

AttachmentPromiseNodeBase::AttachmentPromiseNodeBase(Own<PromiseNode>&& dependency)
    : dependency(kj::mv(dependency)) {}

bool AttachmentPromiseNodeBase::onReady(EventLoop::Event& event) noexcept {
  return dependency->onReady(event);
}

void AttachmentPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  dependency->get(output);
}

Maybe<const EventLoop&> AttachmentPromiseNodeBase::getSafeEventLoop() noexcept {
  return dependency->getSafeEventLoop();
}

void AttachmentPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

// -------------------------------------------------------------------

TransformPromiseNodeBase::TransformPromiseNodeBase(
    Maybe<const EventLoop&> loop, Own<PromiseNode>&& dependency)
    : loop(loop), dependency(kj::mv(dependency)) {}

bool TransformPromiseNodeBase::onReady(EventLoop::Event& event) noexcept {
  return dependency->onReady(event);
}

void TransformPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    getImpl(output);
    dropDependency();
  })) {
    output.addException(kj::mv(*exception));
  }
}

Maybe<const EventLoop&> TransformPromiseNodeBase::getSafeEventLoop() noexcept {
  return loop == nullptr ? dependency->getSafeEventLoop() : loop;
}

void TransformPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

void TransformPromiseNodeBase::getDepResult(ExceptionOrValue& output) {
  dependency->get(output);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    dependency = nullptr;
  })) {
    output.addException(kj::mv(*exception));
  }
}

// -------------------------------------------------------------------

ForkBranchBase::ForkBranchBase(Own<const ForkHubBase>&& hubParam): hub(kj::mv(hubParam)) {
  auto lock = hub->branchList.lockExclusive();

  if (lock->lastPtr == nullptr) {
    onReadyEvent = _kJ_ALREADY_READY;
  } else {
    // Insert into hub's linked list of branches.
    prevPtr = lock->lastPtr;
    *prevPtr = this;
    next = nullptr;
    lock->lastPtr = &next;
  }
}

ForkBranchBase::~ForkBranchBase() noexcept(false) {
  if (prevPtr != nullptr) {
    // Remove from hub's linked list of branches.
    auto lock = hub->branchList.lockExclusive();
    *prevPtr = next;
    (next == nullptr ? lock->lastPtr : next->prevPtr) = prevPtr;
  }
}

void ForkBranchBase::hubReady() noexcept {
  atomicReady(onReadyEvent);
}

void ForkBranchBase::releaseHub(ExceptionOrValue& output) {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    hub = nullptr;
  })) {
    output.addException(kj::mv(*exception));
  }
}

bool ForkBranchBase::onReady(EventLoop::Event& event) noexcept {
  return atomicOnReady(onReadyEvent, event);
}

Maybe<const EventLoop&> ForkBranchBase::getSafeEventLoop() noexcept {
  // It's safe to read the hub's value from multiple threads, once it is ready, since we'll only
  // be reading a const reference.
  return nullptr;
}

// -------------------------------------------------------------------

ForkHubBase::ForkHubBase(const EventLoop& loop, Own<PromiseNode>&& inner,
                         ExceptionOrValue& resultRef)
    : EventLoop::Event(loop), inner(kj::mv(inner)), resultRef(resultRef) {
  KJ_DREQUIRE(this->inner->isSafeEventLoop(loop));
}

void ForkHubBase::fire() {
  if (!isWaiting && !inner->onReady(*this)) {
    isWaiting = true;
  } else {
    // Dependency is ready.  Fetch its result and then delete the node.
    inner->get(resultRef);
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
      inner = nullptr;
    })) {
      resultRef.addException(kj::mv(*exception));
    }

    auto lock = branchList.lockExclusive();
    for (auto branch = lock->first; branch != nullptr; branch = branch->next) {
      branch->hubReady();
      *branch->prevPtr = nullptr;
      branch->prevPtr = nullptr;
    }
    *lock->lastPtr = nullptr;

    // Indicate that the list is no longer active.
    lock->lastPtr = nullptr;
  }
}

// -------------------------------------------------------------------

ChainPromiseNode::ChainPromiseNode(const EventLoop& loop, Own<PromiseNode> inner)
    : Event(loop), state(PRE_STEP1), inner(kj::mv(inner)) {
  KJ_DREQUIRE(this->inner->isSafeEventLoop(loop));
  arm();
}

ChainPromiseNode::~ChainPromiseNode() noexcept(false) {
  disarm();
}

bool ChainPromiseNode::onReady(EventLoop::Event& event) noexcept {
  switch (state) {
    case PRE_STEP1:
    case STEP1:
      KJ_REQUIRE(onReadyEvent == nullptr, "onReady() can only be called once.");
      onReadyEvent = &event;
      return false;
    case STEP2:
      return inner->onReady(event);
  }
  KJ_UNREACHABLE;
}

void ChainPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(state == STEP2);
  return inner->get(output);
}

Maybe<const EventLoop&> ChainPromiseNode::getSafeEventLoop() noexcept {
  return getEventLoop();
}

void ChainPromiseNode::fire() {
  if (state == PRE_STEP1 && !inner->onReady(*this)) {
    state = STEP1;
    return;
  }

  KJ_REQUIRE(state != STEP2);

  static_assert(sizeof(Promise<int>) == sizeof(PromiseBase),
      "This code assumes Promise<T> does not add any new members to PromiseBase.");

  ExceptionOr<PromiseBase> intermediate;
  inner->get(intermediate);

  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    inner = nullptr;
  })) {
    intermediate.addException(kj::mv(*exception));
  }

  KJ_IF_MAYBE(exception, intermediate.exception) {
    // There is an exception.  If there is also a value, delete it.
    kj::runCatchingExceptions([&,this]() { intermediate.value = nullptr; });
    // Now set step2 to a rejected promise.
    inner = heap<ImmediateBrokenPromiseNode>(kj::mv(*exception));
  } else KJ_IF_MAYBE(value, intermediate.value) {
    // There is a value and no exception.  The value is itself a promise.  Adopt it as our
    // step2.
    inner = kj::mv(value->node);
  } else {
    // We can only get here if inner->get() returned neither an exception nor a
    // value, which never actually happens.
    KJ_FAIL_ASSERT("Inner node returned empty value.");
  }
  state = STEP2;

  if (onReadyEvent != nullptr) {
    if (inner->onReady(*onReadyEvent)) {
      onReadyEvent->arm();
    }
  }
}

// -------------------------------------------------------------------

ExclusiveJoinPromiseNode::ExclusiveJoinPromiseNode(
    const EventLoop& loop, Own<PromiseNode> left, Own<PromiseNode> right)
    : left(loop, *this, kj::mv(left)),
      right(loop, *this, kj::mv(right)) {}

ExclusiveJoinPromiseNode::~ExclusiveJoinPromiseNode() noexcept(false) {}

bool ExclusiveJoinPromiseNode::onReady(EventLoop::Event& event) noexcept {
  if (onReadyEvent == _kJ_ALREADY_READY) {
    return true;
  } else {
    onReadyEvent = &event;
    return false;
  }
}

void ExclusiveJoinPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(left.get(output) || right.get(output),
             "get() called before ready.");
}

Maybe<const EventLoop&> ExclusiveJoinPromiseNode::getSafeEventLoop() noexcept {
  return left.getEventLoop();
}

ExclusiveJoinPromiseNode::Branch::Branch(
    const EventLoop& loop, ExclusiveJoinPromiseNode& joinNode, Own<PromiseNode> dependency)
    : Event(loop), joinNode(joinNode), dependency(kj::mv(dependency)) {
  KJ_DREQUIRE(this->dependency->isSafeEventLoop(loop));
  arm();
}

ExclusiveJoinPromiseNode::Branch::~Branch() noexcept(false) {
  disarm();
}

bool ExclusiveJoinPromiseNode::Branch::get(ExceptionOrValue& output) {
  if (finished) {
    dependency->get(output);
    return true;
  } else {
    return false;
  }
}

void ExclusiveJoinPromiseNode::Branch::fire() {
  if (!isWaiting && !dependency->onReady(*this)) {
    isWaiting = true;
  } else {
    finished = true;

    // Cancel the branch that didn't return first.  Ignore exceptions caused by cancellation.
    if (this == &joinNode.left) {
      joinNode.right.disarm();
      kj::runCatchingExceptions([&]() { joinNode.right.dependency = nullptr; });
    } else {
      joinNode.left.disarm();
      kj::runCatchingExceptions([&]() { joinNode.left.dependency = nullptr; });
    }

    if (joinNode.onReadyEvent == nullptr) {
      joinNode.onReadyEvent = _kJ_ALREADY_READY;
    } else {
      joinNode.onReadyEvent->arm();
    }
  }
}

// -------------------------------------------------------------------

CrossThreadPromiseNodeBase::CrossThreadPromiseNodeBase(
    const EventLoop& loop, Own<PromiseNode>&& dependency, ExceptionOrValue& resultRef)
    : Event(loop), dependency(kj::mv(dependency)), resultRef(resultRef) {
  KJ_DREQUIRE(this->dependency->isSafeEventLoop(loop));
}

bool CrossThreadPromiseNodeBase::onReady(EventLoop::Event& event) noexcept {
  return PromiseNode::atomicOnReady(onReadyEvent, event);
}

Maybe<const EventLoop&> CrossThreadPromiseNodeBase::getSafeEventLoop() noexcept {
  return nullptr;
}

void CrossThreadPromiseNodeBase::fire() {
  if (!isWaiting && !dependency->onReady(*this)) {
    isWaiting = true;
  } else {
    dependency->get(resultRef);
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
      dependency = nullptr;
    })) {
      resultRef.addException(kj::mv(*exception));
    }

    // If onReadyEvent is null, set it to _kJ_ALREADY_READY.  Otherwise, arm it.
    PromiseNode::atomicReady(onReadyEvent);
  }
}

// -------------------------------------------------------------------

bool AdapterPromiseNodeBase::onReady(EventLoop::Event& event) noexcept {
  return PromiseNode::atomicOnReady(onReadyEvent, event);
}

Maybe<const EventLoop&> AdapterPromiseNodeBase::getSafeEventLoop() noexcept {
  // We're careful to be thread-safe so any thread is OK.
  return nullptr;
}

}  // namespace _ (private)
}  // namespace kj
