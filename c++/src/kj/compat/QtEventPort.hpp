// Copyright (c) 2015 Follow My Vote, Inc.
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

#ifndef QTEVENTPORT
#define QTEVENTPORT

#include <QObject>

#include <kj/async.h>

class QtEventPort : public QObject, public kj::EventPort
{
    // Simple EventPort implementation to allow a KJ event loop to run in a thread scheduled by a Qt event loop
    // Make sure to call setLoop with a pointer to the KJ event loop which will be sharing this thread as soon as
    // possible after construction.
    Q_OBJECT
public:
    virtual ~QtEventPort();

    void setLoop(kj::EventLoop* kjLoop) {
        // Store a pointer to the KJ event loop to call run() on when it needs to process events. The QtEventPort does
        // not take ownership of kjLoop. Make sure to call setLoop(nullptr) or destroy the QtEvenPort prior to
        // deallocating kjLoop.
        this->kjLoop = kjLoop;
    }

    // EventPort API
    virtual bool wait();
    virtual bool poll();
    virtual void setRunnable(bool runnable);

private:
    bool isRunnable = false;
    kj::EventLoop* kjLoop = nullptr;

private slots:
    void run();
};

#endif // QTEVENTPORT

