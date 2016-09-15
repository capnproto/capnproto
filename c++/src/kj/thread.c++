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

#include "thread.h"
#include "debug.h"
#include "mutex.h"

#if _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#endif

namespace kj {
namespace _ {
  struct SharedObjects {
    // Structure to hold all the data that is shared by the created
    // and creating threads. Wrapped into one struct rather than
    // locking and unlocking each individual member.
    Function<void()> func;
    kj::Maybe<kj::Exception> exception;
    int refCount = 0;
    //SharedObjects() = default;
    SharedObjects( Function<void()> func ) : func(kj::mv(func)) {}
  };

  class CustomSharedPtr {
    // Similar to std::shared_ptr in intention, but avoids bringing the std lib into kj
    // and the data is kj::MutexGuarded so that different threads can access it.
    // N.B. each thread should have its own instance of this class. The data it points
    // to can be shared safely, but not instances of this class.
  public:
    typedef MutexGuarded<SharedObjects>* orphaned_ref;

    template<typename... T_params>
    static CustomSharedPtr create( T_params&&... args ) {
      CustomSharedPtr result( new MutexGuarded<SharedObjects>( kj::fwd<T_params>(args)... ) );
      ++result.ptr->lockExclusive()->refCount;
      return result;
    }

    static CustomSharedPtr adopt( orphaned_ref orphanedPtr ) {
      // Creates a new reference to already existing data, but does
      // *not* increment the reference count. This is so that unowned
      // references created when the release() method is used can be
      // picked up. Each call to release() *must* be matched with a
      // call to adopt() or memory leaks will occur.
      return CustomSharedPtr( orphanedPtr );
    }

    orphaned_ref get() const {
      // Returns a pointer that can be used in adopt(), but does *not*
      // release ownership. This is so that the pointer can be used in
      // some call that may or may not be successful, before deciding
      // whether to release ownership.
      return ptr;
    }

    orphaned_ref release() {
      // Returns a pointer that can be used in adopt() and releases
      // ownership. Each call to this method *must* be matched with
      // a call to adopt() or memory will leak.
      auto ptrCopy=ptr;
      ptr=nullptr;
      return ptrCopy;
    }

    ~CustomSharedPtr() {
      if( ptr )
      {
        unsigned refCount= --ptr->lockExclusive()->refCount;
        if( refCount==0 ) delete ptr;
      }
    }

    CustomSharedPtr( const CustomSharedPtr& other ) : ptr(other.ptr) {
      if( ptr ) ++ptr->lockExclusive()->refCount;
    }

    CustomSharedPtr( CustomSharedPtr&& other ) : ptr(other.ptr) {
      other.ptr=nullptr;
    }

    inline const MutexGuarded<SharedObjects>* operator->() const { return ptr; }

    // In theory these can have implementations, but they're not required
    // in the current code so why complicate things?
    CustomSharedPtr& operator=( const CustomSharedPtr& other ) = delete;
    CustomSharedPtr& operator=( CustomSharedPtr&& other ) = delete;
  protected:
    explicit CustomSharedPtr( MutexGuarded<SharedObjects>* ptr ) : ptr(ptr) {}
    MutexGuarded<SharedObjects>* ptr;
  };

} // end of "namespace _"

struct Thread::Impl {
  _::CustomSharedPtr sharedData;
  template<typename... T_params>
  Impl( T_params&&... args ) : sharedData( _::CustomSharedPtr::create( kj::fwd<T_params>(args)... ) ) {}
};

#if _WIN32

Thread::Thread(Function<void()> func): impl(heap<Impl>(kj::mv(func))) {
  _::CustomSharedPtr tempRef=impl->sharedData; // this holds a ref in case creating a thread fails
  threadHandle = CreateThread(nullptr, 0, &runThread, this, 0, nullptr);
  KJ_ASSERT(threadHandle != nullptr, "CreateThread failed.");
  tempRef.release(); // if an exception is thrown, this gets skipped and tempRef clears the reference
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    KJ_ASSERT(WaitForSingleObject(threadHandle, INFINITE) != WAIT_FAILED);

    KJ_IF_MAYBE(e, impl->sharedData->lockExclusive()->exception) {
      kj::throwRecoverableException(kj::mv(*e));
    }
  }
}

void Thread::detach() {
  KJ_ASSERT(CloseHandle(threadHandle));
  detached = true;
}

DWORD Thread::runThread(void* ptr) {
  _::CustomSharedPtr sharedData=_::CustomSharedPtr::adopt( reinterpret_cast<_::CustomSharedPtr::orphaned_ref>(ptr) );
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    sharedData->lockExclusive()->func();
  })) {
    sharedData->lockExclusive()->exception = kj::mv(*exception);
  }
  return 0;
}

#else  // _WIN32

Thread::Thread(Function<void()> func): impl(heap<Impl>(kj::mv(func))) {
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");

  _::CustomSharedPtr tempRef=impl->sharedData; // this holds a ref in case creating a thread fails
  int pthreadResult = pthread_create(reinterpret_cast<pthread_t*>(&threadId),
                                     nullptr, &runThread, tempRef.get() );
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_create", pthreadResult);
  }
  else tempRef.release(); // release the reference so that the new thread can take it up
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    int pthreadResult = pthread_join(*reinterpret_cast<pthread_t*>(&threadId), nullptr);
    if (pthreadResult != 0) {
      KJ_FAIL_SYSCALL("pthread_join", pthreadResult) { break; }
    }

    KJ_IF_MAYBE(e, impl->sharedData->lockExclusive()->exception) {
      kj::throwRecoverableException(kj::mv(*e));
    }
  }
}

void Thread::sendSignal(int signo) {
  int pthreadResult = pthread_kill(*reinterpret_cast<pthread_t*>(&threadId), signo);
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_kill", pthreadResult) { break; }
  }
}

void Thread::detach() {
  int pthreadResult = pthread_detach(*reinterpret_cast<pthread_t*>(&threadId));
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_detach", pthreadResult) { break; }
  }
  detached = true;
}

void* Thread::runThread(void* ptr) {
  _::CustomSharedPtr sharedData=_::CustomSharedPtr::adopt( reinterpret_cast<_::CustomSharedPtr::orphaned_ref>(ptr) );
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    sharedData->lockExclusive()->func();
  })) {
    sharedData->lockExclusive()->exception = kj::mv(*exception);
  }
  return nullptr;
}

#endif  // _WIN32, else

}  // namespace kj
