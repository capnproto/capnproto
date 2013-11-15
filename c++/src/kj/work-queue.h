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

#ifndef KJ_WORK_QUEUE_H_
#define KJ_WORK_QUEUE_H_

#include "memory.h"
#include "mutex.h"

namespace kj {
namespace _ {  // private

void yieldThread();

class NewJobCallback {
public:
  virtual void receivedNewJob() const = 0;
};

template <typename Job>
class WorkQueue {
  // A lock-free queue/list of jobs with multiple producers and one consumer.  Multiple
  // threads can add jobs to the queue and one thread can inspect the current jobs and possibly
  // mark them completed.  The jobs don't necessarily need to be completed in order.
  //
  // Job has two methods: complete() and cancel().  Exactly one of these methods will be called,
  // exactly once.  cancel() is called if the application discards the Own<Job> returned by add()
  // before complete() gets a chance to be called.  Often, cancel() does nothing.  Either
  // complete() or cancel() will have been called and returned before Own<Job>'s destructor
  // finishes; the destructor will block if complete() is still running in another thread.
  //
  // This class is extraordinarily difficult to use correctly and therefore remains private for
  // now.

public:
  ~WorkQueue() noexcept(false);

  class JobWrapper final: private Disposer {
    // Holds one Job in the queue.

  public:
    const Job& get() const;
    // Get the wrapped Job.

    // methods called by the consumer ------------------------------------------

    Maybe<JobWrapper&> getNext();
    // Gets the next job in the queue.  Returns nullptr if there are no more jobs.  Removes any
    // jobs in the list that are already canceled.

    template <typename Param>
    Maybe<JobWrapper&> complete(Param&& param);
    // If the listener has not yet dropped its pointer to the Job, invokes the job's `complete()`
    // method with the given parameters and then removes it from the list.  Returns the next job
    // in the list.
    //
    // TODO(someday):  This ought to be a variadic template, letting you pass any number of
    //   params.  However, GCC 4.7 and 4.8 appear to get confused by param packs used inside
    //   lambdas.  Clang handles it fine.

    // methods called by the producer ------------------------------------------

    void addToQueue();
    // Add the job to the queue.
    //
    // It is an error to call this if the job is already in the queue.  However, once complete()
    // has been called on the underlying `Job`, `addToQueue()` can be called again (including
    // recursively during `complete()`.  On the other hand, it is *not* safe to call `addToQueue()`
    // after `cancel()`.

    void cancel();
    // Cancels the job before completion, preventing complete() from being called.  If complete()
    // is already being called, blocks until it finishes.  Causes subsequent calls to `addToQueue()`
    // to have no effect.

    // methods called by the single producer/consumer --------------------------

    void insertAfter(Maybe<JobWrapper&> position, WorkQueue& queue);
    // Like `addToQueue()`, but insert the job into the queue after `position` or the beginning of
    // the queue if `position` is null.  This can only be used if the calling thread is also the
    // consuming thread.
    //
    // The non-const WorkQueue must be provided to allow thread-unsafe access.

  private:
    const WorkQueue& queue;
    Job job;

    mutable _::Once once;
    mutable uint refcount = 1;

    JobWrapper* next = nullptr;
    // The JobWrapper cannot be destroyed until this pointer becomes non-null.

    JobWrapper** prev = nullptr;
    // Pointer to the pointer that points to this.

    friend class WorkQueue;

    template <typename... Params>
    JobWrapper(const WorkQueue& queue, Params&&... params);

    void disposeImpl(void* pointer) const override;
    // Called when the listener discards its Own<Job>.
    //
    // If a run is in-progress in another thread, blocks until it completes.
    //
    // Once both complete() and done() have been called, the job wrapper is deleted.
    //
    // `drop()` is also responsible for calling the job's destructor.

    typename WorkQueue<Job>::JobWrapper* unlink();
    // Safely remove the job from the queue.  Only the consumer can call this.  Does not call
    // removeRef(); the caller must arrange to call this afterwards.  Returns the following item
    // in the queue, if any.

    void removeRef() const;

    template <typename Func>
    void doOnce(Func&& func) const;
    // Execute the given function once.
    //
    // TODO(cleanup):  Perhaps there should be an easier way to do this, without the extra
    //   overhead of Lazy<T>.

    Maybe<JobWrapper&> skipDead();
    // Find the first JobWrapper in the list (starting from this one) which hasn't already
    // run.  Remove any that are already-run.
  };

  Maybe<JobWrapper&> peek(kj::Maybe<NewJobCallback&> callback);
  // Get the first job in the list, or null if the list is empty.  Must only be called in the
  // thread which is receiving the work.
  //
  // `callback` will be invoked in the future, if and when new work is added, to let the caller
  // know that it's necessary to peek again.  The callback will be called at most once.  False
  // alarms are possible; you must actually peek() again to find out if there is new work.  If
  // you are sure that you don't need to be notified, pass null for the callback.

  template <typename... Params>
  Own<JobWrapper> createJob(Params&&... params) const;
  // Creates a new job, passing the given parameters to the `Job` constructor, but does not yet
  // add the job to the queue.  Call addToQueue() on the `JobWrapper` to do so.
  //
  // If the caller discards the returned pointer before the job completes, it will be canceled.
  // Either the job's complete() or cancel() method will be called exactly once and will have
  // returned before the Own<const Job>'s destructor finishes.

private:
  JobWrapper* head = nullptr;
  // Pointer to the first job.

  mutable JobWrapper** tail = &head;
  // Points to the `next` pointer of the last-added Job.  When adding a new Job, `tail` is
  // atomically updated in order to create a well-defined list ordering.  Only after successfully
  // atomically updating `tail` to point at the new job's `next` can the old `tail` location be
  // updated to point at the new job.
  //
  // The consumer does not pay any attention to `tail`.  It only follows `next` pointer until it
  // finds one which is null.

  mutable NewJobCallback* newJobCallback = nullptr;
  // If non-null, run this the next time `add()` is called, then set null.

  void notifyNewJob() const;
  // Atomically call the new job callback (if non-null) and then set it to null.
};

template <typename Job>
WorkQueue<Job>::~WorkQueue() noexcept(false) {
  JobWrapper* ptr = head;
  while (ptr != nullptr) {
    JobWrapper* next = ptr->next;
    ptr->removeRef();
    ptr = next;
  }
}

template <typename Job>
Maybe<typename WorkQueue<Job>::JobWrapper&> WorkQueue<Job>::peek(
    kj::Maybe<NewJobCallback&> callback) {
  KJ_IF_MAYBE(c, callback) {
    __atomic_store_n(&newJobCallback, c, __ATOMIC_RELEASE);
  } else {
    __atomic_store_n(&newJobCallback, nullptr, __ATOMIC_RELEASE);
  }

  JobWrapper* headCopy = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
  if (headCopy == nullptr) {
    return nullptr;
  } else {
    return headCopy->skipDead();
  }
}

template <typename Job>
template <typename... Params>
Own<typename WorkQueue<Job>::JobWrapper> WorkQueue<Job>::createJob(Params&&... params) const {
  JobWrapper* job = new JobWrapper(*this, kj::fwd<Params>(params)...);
  return Own<JobWrapper>(job, *job);
}

template <typename Job>
void WorkQueue<Job>::notifyNewJob() const {
  // Check if there is a NewJobCallback.
  if (__atomic_load_n(&newJobCallback, __ATOMIC_RELAXED) != nullptr) {
    // There is a callback.  Atomically acquire it so that it is only caleld once.
    NewJobCallback* callback = __atomic_exchange_n(&newJobCallback, nullptr, __ATOMIC_ACQUIRE);
    if (callback != nullptr) {
      // Acquired by this thread.  Call it.
      callback->receivedNewJob();
    }
  }
}

template <typename Job>
inline const Job& WorkQueue<Job>::JobWrapper::get() const {
  return job;
}

template <typename Job>
Maybe<typename WorkQueue<Job>::JobWrapper&> WorkQueue<Job>::JobWrapper::getNext() {
  JobWrapper* nextCopy = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
  if (nextCopy == nullptr) {
    return nullptr;
  } else {
    return nextCopy->skipDead();
  }
}

template <typename Job>
template <typename Param>
Maybe<typename WorkQueue<Job>::JobWrapper&> WorkQueue<Job>::JobWrapper::complete(Param&& param) {
  // Remove from the queue first, so that it can be re-added during the callback.
  JobWrapper* result = unlink();

  // Run the callback.
  {
    KJ_DEFER(removeRef());
    doOnce([&]() {
      const_cast<Job&>(job).complete(kj::fwd<Param>(param));
    });
  }

  // Return the next job.
  if (result == nullptr) {
    return nullptr;
  } else {
    return result->skipDead();
  }
}

template <typename Job>
template <typename... Params>
inline WorkQueue<Job>::JobWrapper::JobWrapper(const WorkQueue& queue, Params&&... params)
    : queue(queue), job(kj::fwd<Params>(params)...), once(true) {}

template <typename Job>
void WorkQueue<Job>::JobWrapper::disposeImpl(void* pointer) const {
  KJ_IREQUIRE(once.isDisabled());
  removeRef();
}

template <typename Job>
typename WorkQueue<Job>::JobWrapper* WorkQueue<Job>::JobWrapper::unlink() {
  JobWrapper* result;

  result = *prev = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
  if (result == nullptr) {
    // Oops, we're at the end of the queue.  We need to back up the tail pointer.
    JobWrapper** oldTail = &next;
    if (!__atomic_compare_exchange_n(&queue.tail, &oldTail, prev, false,
                                     __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      // Crap, another thread is busy inserting.  Gotta wait for it.
      do {
        yieldThread();
        result = *prev = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
      } while (result == nullptr);
      result->prev = prev;
    }
  } else {
    result->prev = prev;
  }

  next = nullptr;
  prev = nullptr;

  return result;
}

template <typename Job>
void WorkQueue<Job>::JobWrapper::removeRef() const {
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_ACQ_REL) == 0) {
    delete this;
  }
}

template <typename Job>
template <typename Func>
void WorkQueue<Job>::JobWrapper::doOnce(Func&& func) const {
  class InitializerImpl: public _::Once::Initializer {
  public:
    InitializerImpl(Func& func): func(func) {}

    void run() override {
      func();
    }

  private:
    Func& func;
  };
  InitializerImpl init(func);
  once.runOnce(init);
}

template <typename Job>
Maybe<typename WorkQueue<Job>::JobWrapper&> WorkQueue<Job>::JobWrapper::skipDead() {
  JobWrapper* current = this;

  while (current->once.isInitialized()) {
    // This job has already been taken care of.  Remove it.
    current = current->unlink();
    if (current == nullptr) {
      return nullptr;
    }
  }

  return *current;
}

template <typename Job>
void WorkQueue<Job>::JobWrapper::addToQueue() {
  KJ_IREQUIRE(prev == nullptr, "Job is already in the queue.");
  once.reset();

  // Increment refcount to indicate insertion into the queue.
  __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);

  // Atomically set our `prev` to the tail pointer, and the tail pointer point to our `next`.
  prev = __atomic_load_n(&queue.tail, __ATOMIC_ACQUIRE);
  while (!__atomic_compare_exchange_n(&queue.tail, &prev, &next, true,
                                      __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
    // Oops, someone else updated the tail pointer.  Try again.
  }

  // Update the target of the prev pointer to point back to us.  This inserts the job into the
  // list; the consumer thread may now consume it.
  __atomic_store_n(prev, this, __ATOMIC_RELEASE);

  queue.notifyNewJob();
}

template <typename Job>
void WorkQueue<Job>::JobWrapper::cancel() {
  once.disable();
}

template <typename Job>
void WorkQueue<Job>::JobWrapper::insertAfter(Maybe<JobWrapper&> position, WorkQueue& queue) {
  KJ_IREQUIRE(prev == nullptr, "Job is already in the queue.");
  once.reset();

  // Increment refcount to indicate insertion into the queue.
  __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);


  KJ_IF_MAYBE(p, position) {
    KJ_IREQUIRE(p->prev != nullptr, "position is not in the queue");

    prev = &p->next;
  } else {
    prev = &queue.head;
  }

  next = __atomic_load_n(prev, __ATOMIC_ACQUIRE);

  if (next == nullptr) {
    // It appears we're trying to insert at the end of the queue, so we could interfere with other
    // threads.  We need to update the tail pointer to claim our spot.

    if (!__atomic_compare_exchange_n(&queue.tail, &prev, &next, false,
                                     __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
      // Oh, someone else claimed the spot.  Our job needs to be inserted ahead of theirs so
      // we'll need to spin until they finish linking their job into the list.
      do {
        yieldThread();
        next = __atomic_load_n(prev, __ATOMIC_ACQUIRE);
      } while (next == nullptr);
      next->prev = &next;
    }
  } else {
    next->prev = &next;
  }

  // No more thread interference.
  *prev = this;

  queue.notifyNewJob();
}

}  // namespace _ (private)
}  // namespace kj

#endif  // KJ_WORK_QUEUE_H_
