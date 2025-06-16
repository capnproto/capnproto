// Copyright (c) 2025 Cloudflare, Inc.
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

#pragma once

#include <kj/async.h>
#include <kj/debug.h>
#include <cstdlib> // For abort()

namespace kj {

// =======================================================================================
// PromisesUnordered<T>
// =======================================================================================
// A collection of promises where you can await them in the order they become ready.
//
// PromisesUnordered<T> lets you add multiple promises and then consume them as they complete,
// regardless of the order they were added. This class supports standard C++ iteration semantics,
// making it easy to process promises in a range-based for loop.
//
// QUICK REFERENCE:
// - PromisesUnordered<T>()      : Create a new collection
// - add(Promise<T>&&)           : Add a promise to the collection
// - isEmpty()                   : Check if the collection is empty
// - begin(), end()              : Iterators for range-based for loops
//
// Basic usage:
//   PromisesUnordered<int> pui;
//   pui.add(promise1);
//   pui.add(promise2);
//   pui.add(promise3);
//
//   for (Promise<int> readyPromise : pui) {
//     int value = readyPromise.wait(waitScope);
//     // Process the value...
//   }
//
// Iterator behavior:
// - Dereferencing begin() returns a Promise<T> that resolves when any promise becomes ready
// - The begin() promise consumes the earliest-ready promise when awaited
// - Each iteration of the for loop gets the next ready promise in completion order
// - The iteration ends when all promises have been consumed
//
// Exception handling:
// - Exceptions from promises are preserved and propagated through the Promise<T> returned by iterator
// - When a promise rejects, its exception is not thrown until the Promise<T> is awaited
// - The destructor captures and propagates the first exception thrown by any task's destructor
// - In the case of multiple exceptions from task destructors, only the first is propagated
// - If both a promise and its destructor throw, the promise's exception takes precedence
//
// Cancellation:
// - Cancelling a promise from iterator dereferencing does not cancel the underlying promises
// - Tasks are only cancelled when PromisesUnordered is destroyed or goes out of scope
// - If PromisesUnordered is destroyed while a promise from begin() exists, the process aborts
//
// PromisesUnordered<T> differs from TaskSet in several important ways:
// - It doesn't have an error handler (exceptions propagate through the returned promises)
// - It doesn't eagerly consume promises (they're only evaluated when you iterate)
// - It has begin()/end() iterators instead of onEmpty()
// - It returns values/exceptions from each promise rather than discarding them
//
// Note: This API cannot fully replicate kj::joinPromises() semantics. With joinPromises(),
// continuations on all promises are deferred until *all* promises are ready, and only then
// are they consumed. PromisesUnordered requires consuming each promise in order of completion
// before accessing the next one, making it impossible to implement the barrier-like behavior
// of joinPromises(). This limitation is intentional, as joinPromises() semantics can be
// surprising to developers who often don't expect or need their continuations to be deferred.

template <typename T>
class PromisesUnordered {
public:
  // Standard iterator type for range-based for loops
  class Iterator;
  
  // Create a new PromisesUnordered collection
  // The location parameter is used for debugging and will appear in traces
  inline PromisesUnordered(SourceLocation location = {}): location(location) {}
  
  // Destroys the PromisesUnordered and all tasks it contains
  // Note: If any task destructors throw exceptions, the first exception will be propagated
  // after all tasks have been properly destroyed.
  ~PromisesUnordered();
  
  // This class cannot be copied or moved because the ReadyTaskPromiseNode contains
  // a reference to this object. Moving would invalidate that reference.
  KJ_DISALLOW_COPY_AND_MOVE(PromisesUnordered);
  
  // Add a promise to the collection
  // The promise will be tracked and its result will be available via iteration
  // when it completes.
  void add(Promise<T>&& promise);

  // Returns true if there are no promises in this collection (neither pending nor ready)
  bool isEmpty() const { return pendingTasks == kj::none && readyTasks == kj::none; }
  
  // Returns an iterator to the beginning of the collection
  // If the collection is empty, returns the end iterator
  Iterator begin() {
    return isEmpty() ? end() : Iterator(this);
  }
  
  // Returns an iterator representing the end of the collection
  // This iterator cannot be dereferenced or incremented
  Iterator end() {
    return Iterator(nullptr);
  }
  
  // Returns a string representation of all promises in this collection
  // Useful for debugging promise chains and their state
  kj::String trace();
  
  // Iterator API for range-based for loops and manual iteration
  // 
  // NOTE: This implementation provides just enough functionality to work with range-based
  // for loops and for simple manual iteration. It does not attempt to fulfill all the
  // requirements of any standard C++ iterator concept (like InputIterator). The implementation
  // is intentionally minimal but safe:
  //
  // - Dereferencing returns a Promise<T> for the next ready value
  // - Incrementing resets the iterator to point to the next ready value (if any)
  // - Comparison operators determine if iteration should continue
  //
  // This design prioritizes simplicity and safety over conforming to standard iterator
  // requirements, making it easier to reason about the behavior.
  class Iterator {
  public:
    // Dereferencing the iterator returns a Promise<T> that resolves when any
    // promise in the collection becomes ready.
    Promise<T> operator*() { 
      auto& pu = KJ_REQUIRE_NONNULL(owner, "Cannot dereference end iterator");
      return pu.next(); 
    }
    
    // Increment operator (for ++it syntax)
    // This advances to the next promise in the collection
    Iterator& operator++() { 
      auto& pu = KJ_REQUIRE_NONNULL(owner, "Cannot increment end iterator");
      *this = pu.begin(); 
      return *this;
    }
    
    // Comparison operators for iterator loops
    bool operator==(const Iterator& other) const { return owner == other.owner; }
    bool operator!=(const Iterator& other) const { return owner != other.owner; }
    
  private:
    friend class PromisesUnordered;
    PromisesUnordered* owner;
    
    // Private constructor used by begin() and end()
    explicit Iterator(PromisesUnordered* ptr) : owner(ptr) {}
  };

private:
  // Forward declarations of implementation classes
  class Task;
  class ReadyTaskPromiseNode;
  
  // Returns a Promise<T> that resolves when any promise in the collection becomes ready.
  // If the collection is empty, returns a broken promise with an appropriate error message.
  Promise<T> next();

  using OwnTask = Own<Task, _::PromiseDisposer>;
  
  // Helper method to safely destroy a linked list of tasks
  // Captures the first exception encountered but continues destroying all tasks
  // 
  // Note on exception handling: This method prioritizes complete cleanup over preserving
  // all exceptions. Only the first exception is captured and propagated, while ensuring
  // all tasks are properly destroyed. This prevents resource leaks even in exceptional cases.
  static void destroyTaskList(Maybe<OwnTask>& list, Maybe<kj::Exception>& firstException);

  // Linked list of pending promises (not yet completed)
  // New promises are added to the head of this list
  Maybe<OwnTask> pendingTasks;
  
  // Linked list of promises that have completed
  // Tasks are moved to this list in the order they complete (FIFO)
  Maybe<OwnTask> readyTasks;
  
  // Pointer to where we should add the next ready task
  // This points to either readyTasks or to the next field of the last task
  Maybe<OwnTask>* readyTasksTail = &readyTasks;
  
  // The promise node created when begin() is dereferenced
  // It's used to notify the awaiting code when tasks become ready
  Maybe<ReadyTaskPromiseNode> nextReadyPromise;
  
  // Source location for debugging (appears in traces)
  SourceLocation location;
};
// Task class that wraps a promise and handles its completion
template <typename T>
class PromisesUnordered<T>::Task final: public _::PromiseArenaMember, public _::Event {
public:
  Task(_::OwnPromiseNode&& nodeParam, PromisesUnordered& promisesUnordered);
  
  // Called by PromiseDisposer when the task is destroyed
  void destroy() override {
    _::freePromise(this);
  }
  
  // Support for promise tracing
  void traceEvent(kj::_::TraceBuilder& builder) override {
    // Forward to the wrapped promise node
    node->tracePromise(builder, false);
  }
  
  // Removes this task from its linked list and returns ownership of it
  OwnTask pop();
  
protected:
  // Called by the event system when the underlying promise completes
  Maybe<Own<Event>> fire() override;
  
private:
  friend class PromisesUnordered;  // Allow PromisesUnordered to access private members

  // Linked list pointers
  Maybe<OwnTask> next;            // Next task in the list
  Maybe<OwnTask>* prev = nullptr; // Pointer to the previous task's next field
  
  // Returns a string representation of this task for debugging
  kj::String trace() {
    void* space[32]{};
    _::TraceBuilder builder(space);
    node->tracePromise(builder, false);
    return kj::str("task: ", builder);
  }
  
  // No need to store the result as we'll get it directly from the node when needed
  
  _::OwnPromiseNode node;         // The wrapped promise
  PromisesUnordered& promisesUnordered;  // Parent collection
};

// Implementation of ReadyTaskPromiseNode
template <typename T>
class PromisesUnordered<T>::ReadyTaskPromiseNode final: public _::PromiseNode {
public:
  explicit ReadyTaskPromiseNode(PromisesUnordered& promisesUnordered)
      : promisesUnordered(promisesUnordered) {}
  
  // Forward tracing to a representative task for better debugging
  void tracePromise(kj::_::TraceBuilder& builder, bool stopAtNextEvent) override {
    if (stopAtNextEvent) return;
    
    // First check for ready tasks
    KJ_IF_SOME(readyTask, promisesUnordered.readyTasks) {
      // Forward to the first ready task
      readyTask->node->tracePromise(builder, false);
      return;
    }
    
    // Next, check for pending tasks
    KJ_IF_SOME(pendingTask, promisesUnordered.pendingTasks) {
      // Forward to the first pending task
      pendingTask->node->tracePromise(builder, false);
      return;
    }
    
    // No tasks to forward to, use this method's address
    builder.add(getMethodStartAddress(implicitCast<PromiseNode&>(*this), &PromiseNode::get));
  }
  
  // Called when the promise is awaited (e.g., in wait() or when co_awaited)
  void get(_::ExceptionOrValue& result) noexcept override {
    auto& typedResult = result.as<_::FixVoid<T>>();
    
    // Consume the first ready task from the queue (FIFO order)
    // If there are no ready tasks, this is a programming error
    auto& readyTask = KJ_ASSERT_NONNULL(promisesUnordered.readyTasks,
                                        "No ready tasks available");
    
    // Get the result directly from the node and remove the task in one go
    readyTask->node->get(typedResult);
    auto taskToDestroy = readyTask->pop();
    
    // If we just removed the last ready task, reset the tail pointer
    if (promisesUnordered.readyTasks == kj::none) {
      promisesUnordered.readyTasksTail = &promisesUnordered.readyTasks;
    }
    
    // Catch and propagate any exceptions from task destruction
    // while ensuring the task is always destroyed
    KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
      taskToDestroy = nullptr;  // Explicitly destroy
    })) {
      typedResult.addException(kj::mv(exception));
    }
  }
  
  // Called when a promise is chained, to set up notification when this promise is ready
  void onReady(_::Event* event) noexcept override {
    if (event != nullptr) {
      // Set up the event for notification
      onReadyEvent.init(event);
      
      // We don't need to arm the event here. The event will be armed:
      // 1. When Iterator::operator*() is called and existing ready tasks are found
      // 2. When Task::fire() is called when a task becomes ready
    }
  }
  
  // Called when the promise is destroyed
  void destroy() override {
    promisesUnordered.nextReadyPromise = kj::none;
  }
  
  // Activates the notification system to signal that a task is ready
  // This method arms the onReadyEvent to notify whoever is waiting for this promise
  // Called in two scenarios: 
  // 1. When a task becomes ready while a promise from next() exists (from Task::fire)
  // 2. When next() is called and ready tasks already exist
  void armReadyEvent() {
    onReadyEvent.arm();
  }
  
private:
  PromisesUnordered& promisesUnordered;  // Reference to the parent collection
  OnReadyEvent onReadyEvent;             // Notification mechanism for ready tasks
};

// Implementations of Task methods
template <typename T>
PromisesUnordered<T>::Task::Task(_::OwnPromiseNode&& nodeParam, PromisesUnordered& promisesUnordered)
    : Event(promisesUnordered.location),
      node(kj::mv(nodeParam)),
      promisesUnordered(promisesUnordered) {
  node->setSelfPointer(&node);
  
  // Register this task as an event listener for when the promise completes
  // This ensures we track when promises are fulfilled, even before iteration begins
  node->onReady(this);
}

template <typename T>
typename PromisesUnordered<T>::OwnTask PromisesUnordered<T>::Task::pop() {
  // Update linked list pointers to remove this task
  KJ_IF_SOME(n, next) { n->prev = prev; }
  
  OwnTask self = kj::mv(KJ_ASSERT_NONNULL(*prev));
  KJ_ASSERT(self.get() == this);
  *prev = kj::mv(next);
  
  // Clear pointers since we're no longer in the list
  next = kj::none;
  prev = nullptr;
  
  return self;
}

template <typename T>
Maybe<Own<_::Event>> PromisesUnordered<T>::Task::fire() {
  // The promise is ready but we won't get the result until needed
  // Just move this task to the ready list
  
  // Check if the ready list is empty before we add to it
  bool wasReadyListEmpty = promisesUnordered.readyTasks == kj::none;
  
  // Remove this task from the pendingTasks list and prepare to add to readyTasks
  auto self = pop();
  next = kj::none;
  prev = promisesUnordered.readyTasksTail;
  
  // Add to the end of the readyTasks list
  *promisesUnordered.readyTasksTail = kj::mv(self);
  promisesUnordered.readyTasksTail = &KJ_ASSERT_NONNULL(*promisesUnordered.readyTasksTail)->next;
  
  // If we just added the first ready task and there's a waiting promise, notify it
  if (wasReadyListEmpty) {
    KJ_IF_SOME(node, promisesUnordered.nextReadyPromise) {
      node.armReadyEvent();
    }
  }
  
  return kj::none;  // No further events to process
}

// Implementation of PromisesUnordered<T> methods
template <typename T>
PromisesUnordered<T>::~PromisesUnordered() {
  // SAFETY CHECK: Prevent use-after-free by ensuring no promises from begin() exist
  if (nextReadyPromise != kj::none) {
    KJ_LOG(FATAL, "PromisesUnordered destroyed while a promise from begin() still exists");
    abort(); // Terminate immediately to prevent use-after-free
  }

  // Clean up tasks safely, handling any exceptions from destructors
  kj::Maybe<kj::Exception> firstException;
  
  // First clean up pending tasks, then ready tasks
  destroyTaskList(pendingTasks, firstException);
  destroyTaskList(readyTasks, firstException);
  
  // If we caught any exceptions during cleanup, re-throw the first one
  KJ_IF_SOME(exception, firstException) {
    kj::throwFatalException(kj::mv(exception));
  }
}

template <typename T>
void PromisesUnordered<T>::add(Promise<T>&& promise) {
  // Create a new task for this promise
  auto task = _::PromiseDisposer::appendPromise<Task>(_::PromiseNode::from(kj::mv(promise)), *this);
  
  // Add the task to our linked list of pending tasks
  KJ_IF_SOME(head, pendingTasks) {
    head->prev = &task->next;
    task->next = kj::mv(pendingTasks);
  }
  task->prev = &pendingTasks;
  pendingTasks = kj::mv(task);
}

// Implementation of PromisesUnordered<T>::trace() method
template <typename T>
kj::String PromisesUnordered<T>::trace() {
  kj::Vector<kj::String> traces;

  // Helper to trace all tasks in a linked list with a specific prefix
  auto addTraces = [&](Maybe<OwnTask>& taskList, kj::StringPtr prefix) {
    Maybe<OwnTask>* ptr = &taskList;
    while (ptr != nullptr) {
      KJ_IF_SOME(task, *ptr) {
        traces.add(kj::str(prefix, " ", task->trace()));
        ptr = &task->next;
      } else {
        break;
      }
    }
  };

  // Add traces for both types of tasks
  addTraces(pendingTasks, "pending");
  addTraces(readyTasks, "ready");

  return kj::strArray(traces, "\n");
}

// Implementation of PromisesUnordered<T>::next() method
template <typename T>
Promise<T> PromisesUnordered<T>::next() {
  // Return a broken promise if the collection is empty
  if (isEmpty()) {
    return Promise<T>(KJ_EXCEPTION(FAILED, "No promises in the collection"));
  }
  
  // Create the ReadyTaskPromiseNode on first next() call
  if (nextReadyPromise == kj::none) {
    // This is the first time next() is being called,
    // so we need to create the ReadyTaskPromiseNode
    auto& node = nextReadyPromise.emplace(*this);
    
    // If we already have ready tasks, immediately arm the event
    // so the returned promise becomes ready right away
    // Note: Task::fire() will only arm for the first ready task, but we must arm here
    // for existing ready tasks that were ready before the promise was created
    if (readyTasks != kj::none) {
      node.armReadyEvent();
    }
    
    // Create a promise that will consume the first ready task when awaited
    return _::PromiseNode::to<Promise<T>>(_::OwnPromiseNode(&node));
  } else {
    // A promise for the next ready task already exists and has not yet been consumed
    return Promise<T>(KJ_EXCEPTION(FAILED,
        "A Promise for the next ready task already exists"));
  }
}

// Implementation of PromisesUnordered<T>::destroyTaskList method
template <typename T>
void PromisesUnordered<T>::destroyTaskList(Maybe<OwnTask>& list, Maybe<kj::Exception>& firstException) {
  while (list != kj::none) {
    auto task = KJ_ASSERT_NONNULL(list)->pop();
    
    // Catch any exceptions from task destructors
    KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
      task = nullptr;  // Explicitly destroy
    })) {
      // Save the first exception we encounter
      if (firstException == kj::none) {
        firstException = kj::mv(exception);
      }
    }
  }
}

}  // namespace kj