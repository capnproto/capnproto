// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "async.h"
#include "debug.h"
#include "vector.h"
#include "threadlocal.h"

#if KJ_USE_FUTEX
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#endif

#if !KJ_NO_RTTI
#include <typeinfo>
#if __GNUC__
#include <cxxabi.h>
#include <stdlib.h>
#endif
#endif

namespace kj {

namespace {

KJ_THREADLOCAL_PTR(EventLoop) threadLocalEventLoop = nullptr;

#define _kJ_ALREADY_READY reinterpret_cast< ::kj::_::Event*>(1)

EventLoop& currentEventLoop() {
  EventLoop* loop = threadLocalEventLoop;
  KJ_REQUIRE(loop != nullptr, "No event loop is running on this thread.");
  return *loop;
}

class BoolEvent: public _::Event {
public:
  bool fired = false;

  Maybe<Own<_::Event>> fire() override {
    fired = true;
    return nullptr;
  }
};

class YieldPromiseNode final: public _::PromiseNode {
public:
  void onReady(_::Event* event) noexcept override {
    if (event) event->armBreadthFirst();
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    output.as<_::Void>() = _::Void();
  }
};

class NeverDonePromiseNode final: public _::PromiseNode {
public:
  void onReady(_::Event* event) noexcept override {
    // ignore
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    KJ_FAIL_REQUIRE("Not ready.");
  }
};

}  // namespace

// =======================================================================================

Canceler::~Canceler() noexcept(false) {
  cancel("operation canceled");
}

void Canceler::cancel(StringPtr cancelReason) {
  if (isEmpty()) return;
  cancel(Exception(Exception::Type::FAILED, __FILE__, __LINE__, kj::str(cancelReason)));
}

void Canceler::cancel(const Exception& exception) {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      list = a->next;
      a->prev = nullptr;
      a->next = nullptr;
      a->cancel(kj::cp(exception));
    } else {
      break;
    }
  }
}

void Canceler::release() {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      list = a->next;
      a->prev = nullptr;
      a->next = nullptr;
    } else {
      break;
    }
  }
}

Canceler::AdapterBase::AdapterBase(Canceler& canceler)
    : prev(canceler.list),
      next(canceler.list) {
  canceler.list = *this;
  KJ_IF_MAYBE(n, next) {
    n->prev = next;
  }
}

Canceler::AdapterBase::~AdapterBase() noexcept(false) {
  KJ_IF_MAYBE(p, prev) {
    *p = next;
  }
  KJ_IF_MAYBE(n, next) {
    n->prev = prev;
  }
}

Canceler::AdapterImpl<void>::AdapterImpl(kj::PromiseFulfiller<void>& fulfiller,
            Canceler& canceler, kj::Promise<void> inner)
    : AdapterBase(canceler),
      fulfiller(fulfiller),
      inner(inner.then(
          [&fulfiller]() { fulfiller.fulfill(); },
          [&fulfiller](kj::Exception&& e) { fulfiller.reject(kj::mv(e)); })
          .eagerlyEvaluate(nullptr)) {}

void Canceler::AdapterImpl<void>::cancel(kj::Exception&& e) {
  fulfiller.reject(kj::mv(e));
  inner = nullptr;
}

// =======================================================================================

TaskSet::TaskSet(TaskSet::ErrorHandler& errorHandler)
  : errorHandler(errorHandler) {}

TaskSet::~TaskSet() noexcept(false) {}

class TaskSet::Task final: public _::Event {
public:
  Task(TaskSet& taskSet, Own<_::PromiseNode>&& nodeParam)
      : taskSet(taskSet), node(kj::mv(nodeParam)) {
    node->setSelfPointer(&node);
    node->onReady(this);
  }

  Maybe<Own<Task>> next;
  Maybe<Own<Task>>* prev = nullptr;

protected:
  Maybe<Own<Event>> fire() override {
    // Get the result.
    _::ExceptionOr<_::Void> result;
    node->get(result);

    // Delete the node, catching any exceptions.
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
      node = nullptr;
    })) {
      result.addException(kj::mv(*exception));
    }

    // Call the error handler if there was an exception.
    KJ_IF_MAYBE(e, result.exception) {
      taskSet.errorHandler.taskFailed(kj::mv(*e));
    }

    // Remove from the task list.
    KJ_IF_MAYBE(n, next) {
      n->get()->prev = prev;
    }
    Own<Event> self = kj::mv(KJ_ASSERT_NONNULL(*prev));
    KJ_ASSERT(self.get() == this);
    *prev = kj::mv(next);
    next = nullptr;
    prev = nullptr;

    KJ_IF_MAYBE(f, taskSet.emptyFulfiller) {
      if (taskSet.tasks == nullptr) {
        f->get()->fulfill();
        taskSet.emptyFulfiller = nullptr;
      }
    }

    return mv(self);
  }

  _::PromiseNode* getInnerForTrace() override {
    return node;
  }

private:
  TaskSet& taskSet;
  Own<_::PromiseNode> node;
};

void TaskSet::add(Promise<void>&& promise) {
  auto task = heap<Task>(*this, kj::mv(promise.node));
  KJ_IF_MAYBE(head, tasks) {
    head->get()->prev = &task->next;
    task->next = kj::mv(tasks);
  }
  task->prev = &tasks;
  tasks = kj::mv(task);
}

kj::String TaskSet::trace() {
  kj::Vector<kj::String> traces;

  Maybe<Own<Task>>* ptr = &tasks;
  for (;;) {
    KJ_IF_MAYBE(task, *ptr) {
      traces.add(task->get()->trace());
      ptr = &task->get()->next;
    } else {
      break;
    }
  }

  return kj::strArray(traces, "\n============================================\n");
}

Promise<void> TaskSet::onEmpty() {
  KJ_REQUIRE(emptyFulfiller == nullptr, "onEmpty() can only be called once at a time");

  if (tasks == nullptr) {
    return READY_NOW;
  } else {
    auto paf = newPromiseAndFulfiller<void>();
    emptyFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
}

namespace _ {  // private

class LoggingErrorHandler: public TaskSet::ErrorHandler {
public:
  static LoggingErrorHandler instance;

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, "Uncaught exception in daemonized task.", exception);
  }
};

LoggingErrorHandler LoggingErrorHandler::instance = LoggingErrorHandler();

class NullEventPort: public EventPort {
public:
  bool wait() override {
    KJ_FAIL_REQUIRE("Nothing to wait for; this thread would hang forever.");
  }

  bool poll() override { return false; }

  void wake() const override {
    // TODO(someday): Implement using condvar.
    kj::throwRecoverableException(KJ_EXCEPTION(UNIMPLEMENTED,
        "Cross-thread events are not yet implemented for EventLoops with no EventPort."));
  }

  static NullEventPort instance;
};

NullEventPort NullEventPort::instance = NullEventPort();

}  // namespace _ (private)

// =======================================================================================

void EventPort::setRunnable(bool runnable) {}

void EventPort::wake() const {
  kj::throwRecoverableException(KJ_EXCEPTION(UNIMPLEMENTED,
      "cross-thread wake() not implemented by this EventPort implementation"));
}

EventLoop::EventLoop()
    : port(_::NullEventPort::instance),
      daemons(kj::heap<TaskSet>(_::LoggingErrorHandler::instance)) {}

EventLoop::EventLoop(EventPort& port)
    : port(port),
      daemons(kj::heap<TaskSet>(_::LoggingErrorHandler::instance)) {}

EventLoop::~EventLoop() noexcept(false) {
  // Destroy all "daemon" tasks, noting that their destructors might try to access the EventLoop
  // some more.
  daemons = nullptr;

  // The application _should_ destroy everything using the EventLoop before destroying the
  // EventLoop itself, so if there are events on the loop, this indicates a memory leak.
  KJ_REQUIRE(head == nullptr, "EventLoop destroyed with events still in the queue.  Memory leak?",
             head->trace()) {
    // Unlink all the events and hope that no one ever fires them...
    _::Event* event = head;
    while (event != nullptr) {
      _::Event* next = event->next;
      event->next = nullptr;
      event->prev = nullptr;
      event = next;
    }
    break;
  }

  KJ_REQUIRE(threadLocalEventLoop != this,
             "EventLoop destroyed while still current for the thread.") {
    threadLocalEventLoop = nullptr;
    break;
  }
}

void EventLoop::run(uint maxTurnCount) {
  running = true;
  KJ_DEFER(running = false);

  for (uint i = 0; i < maxTurnCount; i++) {
    if (!turn()) {
      break;
    }
  }

  setRunnable(isRunnable());
}

bool EventLoop::turn() {
  _::Event* event = head;

  if (event == nullptr) {
    // No events in the queue.
    return false;
  } else {
    head = event->next;
    if (head != nullptr) {
      head->prev = &head;
    }

    depthFirstInsertPoint = &head;
    if (tail == &event->next) {
      tail = &head;
    }

    event->next = nullptr;
    event->prev = nullptr;

    Maybe<Own<_::Event>> eventToDestroy;
    {
      event->firing = true;
      KJ_DEFER(event->firing = false);
      eventToDestroy = event->fire();
    }

    depthFirstInsertPoint = &head;
    return true;
  }
}

bool EventLoop::isRunnable() {
  return head != nullptr;
}

void EventLoop::setRunnable(bool runnable) {
  if (runnable != lastRunnableState) {
    port.setRunnable(runnable);
    lastRunnableState = runnable;
  }
}

void EventLoop::enterScope() {
  KJ_REQUIRE(threadLocalEventLoop == nullptr, "This thread already has an EventLoop.");
  threadLocalEventLoop = this;
}

void EventLoop::leaveScope() {
  KJ_REQUIRE(threadLocalEventLoop == this,
             "WaitScope destroyed in a different thread than it was created in.") {
    break;
  }
  threadLocalEventLoop = nullptr;
}

void WaitScope::poll() {
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  loop.running = true;
  KJ_DEFER(loop.running = false);

  for (;;) {
    if (!loop.turn()) {
      // No events in the queue.  Poll for I/O.
      loop.port.poll();

      if (!loop.isRunnable()) {
        // Still no events in the queue. We're done.
        return;
      }
    }
  }
}

namespace _ {  // private

void waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result, WaitScope& waitScope) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(!loop.running, "wait() is not allowed from within event callbacks.");

  BoolEvent doneEvent;
  node->setSelfPointer(&node);
  node->onReady(&doneEvent);

  loop.running = true;
  KJ_DEFER(loop.running = false);

  while (!doneEvent.fired) {
    if (!loop.turn()) {
      // No events in the queue.  Wait for callback.
      loop.port.wait();
    }
  }

  loop.setRunnable(loop.isRunnable());

  node->get(result);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    node = nullptr;
  })) {
    result.addException(kj::mv(*exception));
  }
}

bool pollImpl(_::PromiseNode& node, WaitScope& waitScope) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  BoolEvent doneEvent;
  node.onReady(&doneEvent);

  loop.running = true;
  KJ_DEFER(loop.running = false);

  while (!doneEvent.fired) {
    if (!loop.turn()) {
      // No events in the queue.  Poll for I/O.
      loop.port.poll();

      if (!doneEvent.fired && !loop.isRunnable()) {
        // No progress. Give up.
        node.onReady(nullptr);
        loop.setRunnable(false);
        return false;
      }
    }
  }

  loop.setRunnable(loop.isRunnable());
  return true;
}

Promise<void> yield() {
  return Promise<void>(false, kj::heap<YieldPromiseNode>());
}

Own<PromiseNode> neverDone() {
  return kj::heap<NeverDonePromiseNode>();
}

void NeverDone::wait(WaitScope& waitScope) const {
  ExceptionOr<Void> dummy;
  waitImpl(neverDone(), dummy, waitScope);
  KJ_UNREACHABLE;
}

void detach(kj::Promise<void>&& promise) {
  EventLoop& loop = currentEventLoop();
  KJ_REQUIRE(loop.daemons.get() != nullptr, "EventLoop is shutting down.") { return; }
  loop.daemons->add(kj::mv(promise));
}

Event::Event()
    : loop(currentEventLoop()), next(nullptr), prev(nullptr) {}

Event::~Event() noexcept(false) {
  if (prev != nullptr) {
    if (loop.tail == &next) {
      loop.tail = prev;
    }
    if (loop.depthFirstInsertPoint == &next) {
      loop.depthFirstInsertPoint = prev;
    }

    *prev = next;
    if (next != nullptr) {
      next->prev = prev;
    }
  }

  KJ_REQUIRE(!firing, "Promise callback destroyed itself.");
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Promise destroyed from a different thread than it was created in.");
}

void Event::armDepthFirst() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "the thread-safe work queue to queue events cross-thread.");

  if (prev == nullptr) {
    next = *loop.depthFirstInsertPoint;
    prev = loop.depthFirstInsertPoint;
    *prev = this;
    if (next != nullptr) {
      next->prev = &next;
    }

    loop.depthFirstInsertPoint = &next;

    if (loop.tail == prev) {
      loop.tail = &next;
    }

    loop.setRunnable(true);
  }
}

void Event::armBreadthFirst() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "the thread-safe work queue to queue events cross-thread.");

  if (prev == nullptr) {
    next = *loop.tail;
    prev = loop.tail;
    *prev = this;
    if (next != nullptr) {
      next->prev = &next;
    }

    loop.tail = &next;

    loop.setRunnable(true);
  }
}

_::PromiseNode* Event::getInnerForTrace() {
  return nullptr;
}

#if !KJ_NO_RTTI
#if __GNUC__
static kj::String demangleTypeName(const char* name) {
  int status;
  char* buf = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  kj::String result = kj::heapString(buf == nullptr ? name : buf);
  free(buf);
  return kj::mv(result);
}
#else
static kj::String demangleTypeName(const char* name) {
  return kj::heapString(name);
}
#endif
#endif

static kj::String traceImpl(Event* event, _::PromiseNode* node) {
#if KJ_NO_RTTI
  return heapString("Trace not available because RTTI is disabled.");
#else
  kj::Vector<kj::String> trace;

  if (event != nullptr) {
    trace.add(demangleTypeName(typeid(*event).name()));
  }

  while (node != nullptr) {
    trace.add(demangleTypeName(typeid(*node).name()));
    node = node->getInnerForTrace();
  }

  return strArray(trace, "\n");
#endif
}

kj::String Event::trace() {
  return traceImpl(this, getInnerForTrace());
}

}  // namespace _ (private)

// =======================================================================================

namespace _ {  // private

kj::String PromiseBase::trace() {
  return traceImpl(nullptr, node);
}

void PromiseNode::setSelfPointer(Own<PromiseNode>* selfPtr) noexcept {}

PromiseNode* PromiseNode::getInnerForTrace() { return nullptr; }

void PromiseNode::OnReadyEvent::init(Event* newEvent) {
  if (event == _kJ_ALREADY_READY) {
    // A new continuation was added to a promise that was already ready.  In this case, we schedule
    // breadth-first, to make it difficult for applications to accidentally starve the event loop
    // by repeatedly waiting on immediate promises.
    if (newEvent) newEvent->armBreadthFirst();
  } else {
    event = newEvent;
  }
}

void PromiseNode::OnReadyEvent::arm() {
  KJ_ASSERT(event != _kJ_ALREADY_READY, "arm() should only be called once");

  if (event != nullptr) {
    // A promise resolved and an event is already waiting on it.  In this case, arm in depth-first
    // order so that the event runs immediately after the current one.  This way, chained promises
    // execute together for better cache locality and lower latency.
    event->armDepthFirst();
  }

  event = _kJ_ALREADY_READY;
}

// -------------------------------------------------------------------

ImmediatePromiseNodeBase::ImmediatePromiseNodeBase() {}
ImmediatePromiseNodeBase::~ImmediatePromiseNodeBase() noexcept(false) {}

void ImmediatePromiseNodeBase::onReady(Event* event) noexcept {
  if (event) event->armBreadthFirst();
}

ImmediateBrokenPromiseNode::ImmediateBrokenPromiseNode(Exception&& exception)
    : exception(kj::mv(exception)) {}

void ImmediateBrokenPromiseNode::get(ExceptionOrValue& output) noexcept {
  output.exception = kj::mv(exception);
}

// -------------------------------------------------------------------

AttachmentPromiseNodeBase::AttachmentPromiseNodeBase(Own<PromiseNode>&& dependencyParam)
    : dependency(kj::mv(dependencyParam)) {
  dependency->setSelfPointer(&dependency);
}

void AttachmentPromiseNodeBase::onReady(Event* event) noexcept {
  dependency->onReady(event);
}

void AttachmentPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  dependency->get(output);
}

PromiseNode* AttachmentPromiseNodeBase::getInnerForTrace() {
  return dependency;
}

void AttachmentPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

// -------------------------------------------------------------------

TransformPromiseNodeBase::TransformPromiseNodeBase(
    Own<PromiseNode>&& dependencyParam, void* continuationTracePtr)
    : dependency(kj::mv(dependencyParam)), continuationTracePtr(continuationTracePtr) {
  dependency->setSelfPointer(&dependency);
}

void TransformPromiseNodeBase::onReady(Event* event) noexcept {
  dependency->onReady(event);
}

void TransformPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    getImpl(output);
    dropDependency();
  })) {
    output.addException(kj::mv(*exception));
  }
}

PromiseNode* TransformPromiseNodeBase::getInnerForTrace() {
  return dependency;
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

  KJ_IF_MAYBE(e, output.exception) {
    e->addTrace(continuationTracePtr);
  }
}

// -------------------------------------------------------------------

ForkBranchBase::ForkBranchBase(Own<ForkHubBase>&& hubParam): hub(kj::mv(hubParam)) {
  if (hub->tailBranch == nullptr) {
    onReadyEvent.arm();
  } else {
    // Insert into hub's linked list of branches.
    prevPtr = hub->tailBranch;
    *prevPtr = this;
    next = nullptr;
    hub->tailBranch = &next;
  }
}

ForkBranchBase::~ForkBranchBase() noexcept(false) {
  if (prevPtr != nullptr) {
    // Remove from hub's linked list of branches.
    *prevPtr = next;
    (next == nullptr ? hub->tailBranch : next->prevPtr) = prevPtr;
  }
}

void ForkBranchBase::hubReady() noexcept {
  onReadyEvent.arm();
}

void ForkBranchBase::releaseHub(ExceptionOrValue& output) {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    hub = nullptr;
  })) {
    output.addException(kj::mv(*exception));
  }
}

void ForkBranchBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

PromiseNode* ForkBranchBase::getInnerForTrace() {
  return hub->getInnerForTrace();
}

// -------------------------------------------------------------------

ForkHubBase::ForkHubBase(Own<PromiseNode>&& innerParam, ExceptionOrValue& resultRef)
    : inner(kj::mv(innerParam)), resultRef(resultRef) {
  inner->setSelfPointer(&inner);
  inner->onReady(this);
}

Maybe<Own<Event>> ForkHubBase::fire() {
  // Dependency is ready.  Fetch its result and then delete the node.
  inner->get(resultRef);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    inner = nullptr;
  })) {
    resultRef.addException(kj::mv(*exception));
  }

  for (auto branch = headBranch; branch != nullptr; branch = branch->next) {
    branch->hubReady();
    *branch->prevPtr = nullptr;
    branch->prevPtr = nullptr;
  }
  *tailBranch = nullptr;

  // Indicate that the list is no longer active.
  tailBranch = nullptr;

  return nullptr;
}

_::PromiseNode* ForkHubBase::getInnerForTrace() {
  return inner;
}

// -------------------------------------------------------------------

ChainPromiseNode::ChainPromiseNode(Own<PromiseNode> innerParam)
    : state(STEP1), inner(kj::mv(innerParam)) {
  inner->setSelfPointer(&inner);
  inner->onReady(this);
}

ChainPromiseNode::~ChainPromiseNode() noexcept(false) {}

void ChainPromiseNode::onReady(Event* event) noexcept {
  switch (state) {
    case STEP1:
      onReadyEvent = event;
      return;
    case STEP2:
      inner->onReady(event);
      return;
  }
  KJ_UNREACHABLE;
}

void ChainPromiseNode::setSelfPointer(Own<PromiseNode>* selfPtr) noexcept {
  if (state == STEP2) {
    *selfPtr = kj::mv(inner);  // deletes this!
    selfPtr->get()->setSelfPointer(selfPtr);
  } else {
    this->selfPtr = selfPtr;
  }
}

void ChainPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(state == STEP2);
  return inner->get(output);
}

PromiseNode* ChainPromiseNode::getInnerForTrace() {
  return inner;
}

Maybe<Own<Event>> ChainPromiseNode::fire() {
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
    kj::runCatchingExceptions([&]() { intermediate.value = nullptr; });
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

  if (selfPtr != nullptr) {
    // Hey, we can shorten the chain here.
    auto chain = selfPtr->downcast<ChainPromiseNode>();
    *selfPtr = kj::mv(inner);
    selfPtr->get()->setSelfPointer(selfPtr);
    if (onReadyEvent != nullptr) {
      selfPtr->get()->onReady(onReadyEvent);
    }

    // Return our self-pointer so that the caller takes care of deleting it.
    return Own<Event>(kj::mv(chain));
  } else {
    inner->setSelfPointer(&inner);
    if (onReadyEvent != nullptr) {
      inner->onReady(onReadyEvent);
    }

    return nullptr;
  }
}

// -------------------------------------------------------------------

ExclusiveJoinPromiseNode::ExclusiveJoinPromiseNode(Own<PromiseNode> left, Own<PromiseNode> right)
    : left(*this, kj::mv(left)), right(*this, kj::mv(right)) {}

ExclusiveJoinPromiseNode::~ExclusiveJoinPromiseNode() noexcept(false) {}

void ExclusiveJoinPromiseNode::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ExclusiveJoinPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(left.get(output) || right.get(output), "get() called before ready.");
}

PromiseNode* ExclusiveJoinPromiseNode::getInnerForTrace() {
  auto result = left.getInnerForTrace();
  if (result == nullptr) {
    result = right.getInnerForTrace();
  }
  return result;
}

ExclusiveJoinPromiseNode::Branch::Branch(
    ExclusiveJoinPromiseNode& joinNode, Own<PromiseNode> dependencyParam)
    : joinNode(joinNode), dependency(kj::mv(dependencyParam)) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

ExclusiveJoinPromiseNode::Branch::~Branch() noexcept(false) {}

bool ExclusiveJoinPromiseNode::Branch::get(ExceptionOrValue& output) {
  if (dependency) {
    dependency->get(output);
    return true;
  } else {
    return false;
  }
}

Maybe<Own<Event>> ExclusiveJoinPromiseNode::Branch::fire() {
  // Cancel the branch that didn't return first.  Ignore exceptions caused by cancellation.
  if (this == &joinNode.left) {
    kj::runCatchingExceptions([&]() { joinNode.right.dependency = nullptr; });
  } else {
    kj::runCatchingExceptions([&]() { joinNode.left.dependency = nullptr; });
  }

  joinNode.onReadyEvent.arm();
  return nullptr;
}

PromiseNode* ExclusiveJoinPromiseNode::Branch::getInnerForTrace() {
  return dependency;
}

// -------------------------------------------------------------------

JoinPromiseNodeBase::JoinPromiseNodeBase(
    Own<PromiseNode> left, Own<PromiseNode> right,
    ExceptionOrValue& leftResult, ExceptionOrValue& rightResult,
    bool failfast)
    : countLeft(2), failfast(failfast),
      leftBranch(*this, kj::mv(left), leftResult),
      rightBranch(*this, kj::mv(right), rightResult) {}
JoinPromiseNodeBase::~JoinPromiseNodeBase() noexcept(false) {}

void JoinPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void JoinPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  KJ_IF_MAYBE(exception, leftBranch.getPart()) {
    output.addException(kj::mv(*exception));
  }
  KJ_IF_MAYBE(exception, rightBranch.getPart()) {
    output.addException(kj::mv(*exception));
  }

  if (output.exception == nullptr) {
    // No errors.  The template subclass will need to fill in the result.
    getNoError(output);
  }
}

PromiseNode* JoinPromiseNodeBase::getInnerForTrace() {
  return leftBranch.getInnerForTrace();
}

JoinPromiseNodeBase::Branch::Branch(
    JoinPromiseNodeBase& joinNode, Own<PromiseNode> dependencyParam, ExceptionOrValue& output)
    : joinNode(joinNode), dependency(kj::mv(dependencyParam)), output(output) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

JoinPromiseNodeBase::Branch::~Branch() noexcept(false) {}

Maybe<Own<Event>> JoinPromiseNodeBase::Branch::fire() {
  dependency->get(output);

  if (!joinNode.armed &&
      (--joinNode.countLeft == 0 ||
       (output.exception != nullptr && joinNode.failfast))) {
    joinNode.onReadyEvent.arm();
    joinNode.armed = true;
  }

  return nullptr;
}

_::PromiseNode* JoinPromiseNodeBase::Branch::getInnerForTrace() {
  return dependency->getInnerForTrace();
}

Maybe<Exception> JoinPromiseNodeBase::Branch::getPart() {
  return kj::mv(output.exception);
}

// -------------------------------------------------------------------

ArrayJoinPromiseNodeBase::ArrayJoinPromiseNodeBase(
    Array<Own<PromiseNode>> promises, ExceptionOrValue* resultParts, size_t partSize,
    bool failfast)
    : countLeft(promises.size()), failfast(failfast) {
  // Make the branches.
  auto builder = heapArrayBuilder<Branch>(promises.size());
  for (uint i: indices(promises)) {
    ExceptionOrValue& output = *reinterpret_cast<ExceptionOrValue*>(
        reinterpret_cast<byte*>(resultParts) + i * partSize);
    builder.add(*this, kj::mv(promises[i]), output);
  }
  branches = builder.finish();

  if (branches.size() == 0) {
    onReadyEvent.arm();
  }
}
ArrayJoinPromiseNodeBase::~ArrayJoinPromiseNodeBase() noexcept(false) {}

void ArrayJoinPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ArrayJoinPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  // If any of the elements threw exceptions, propagate them.
  for (auto& branch: branches) {
    KJ_IF_MAYBE(exception, branch.getPart()) {
      output.addException(kj::mv(*exception));
    }
  }

  if (output.exception == nullptr) {
    // No errors.  The template subclass will need to fill in the result.
    getNoError(output);
  }
}

PromiseNode* ArrayJoinPromiseNodeBase::getInnerForTrace() {
  return branches.size() == 0 ? nullptr : branches[0].getInnerForTrace();
}

ArrayJoinPromiseNodeBase::Branch::Branch(
    ArrayJoinPromiseNodeBase& joinNode, Own<PromiseNode> dependencyParam, ExceptionOrValue& output)
    : joinNode(joinNode), dependency(kj::mv(dependencyParam)), output(output) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

ArrayJoinPromiseNodeBase::Branch::~Branch() noexcept(false) {}

Maybe<Own<Event>> ArrayJoinPromiseNodeBase::Branch::fire() {
  dependency->get(output);

  if (!joinNode.armed &&
      (--joinNode.countLeft == 0 ||
       (output.exception != nullptr && joinNode.failfast))) {
    joinNode.onReadyEvent.arm();
    joinNode.armed = true;
  }

  return nullptr;
}

_::PromiseNode* ArrayJoinPromiseNodeBase::Branch::getInnerForTrace() {
  return dependency->getInnerForTrace();
}

Maybe<Exception> ArrayJoinPromiseNodeBase::Branch::getPart() {
  return kj::mv(output.exception);
}

ArrayJoinPromiseNode<void>::ArrayJoinPromiseNode(
    Array<Own<PromiseNode>> promises, Array<ExceptionOr<_::Void>> resultParts, bool failfast)
    : ArrayJoinPromiseNodeBase(kj::mv(promises), resultParts.begin(),
                               sizeof(ExceptionOr<_::Void>), failfast),
      resultParts(kj::mv(resultParts)) {}

ArrayJoinPromiseNode<void>::~ArrayJoinPromiseNode() {}

void ArrayJoinPromiseNode<void>::getNoError(ExceptionOrValue& output) noexcept {
  output.as<_::Void>() = _::Void();
}

}  // namespace _ (private)

Promise<void> joinPromises(Array<Promise<void>>&& promises) {
  return Promise<>(false, kj::heap<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return kj::mv(p.node); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), false));
}

Promise<> joinPromises(Array<Promise<>>&& promises) {
  return Promise<>(false, kj::heap<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return kj::mv(p.node); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), false));
}

Promise<void> joinPromisesFailfast(Array<Promise<void>>&& promises) {
  return Promise<>(false, kj::heap<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return kj::mv(p.node); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), true));
}

Promise<> joinPromisesFailfast(Array<Promise<>>&& promises) {
  return Promise<>(false, kj::heap<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return kj::mv(p.node); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), true));
}

namespace _ {  // (private)

// -------------------------------------------------------------------

EagerPromiseNodeBase::EagerPromiseNodeBase(
    Own<PromiseNode>&& dependencyParam, ExceptionOrValue& resultRef)
    : dependency(kj::mv(dependencyParam)), resultRef(resultRef) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

void EagerPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

PromiseNode* EagerPromiseNodeBase::getInnerForTrace() {
  return dependency;
}

Maybe<Own<Event>> EagerPromiseNodeBase::fire() {
  dependency->get(resultRef);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    dependency = nullptr;
  })) {
    resultRef.addException(kj::mv(*exception));
  }

  onReadyEvent.arm();
  return nullptr;
}

// -------------------------------------------------------------------

void AdapterPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

// -------------------------------------------------------------------

Promise<void> IdentityFunc<Promise<void>>::operator()() const { return READY_NOW; }

}  // namespace _ (private)
}  // namespace kj
