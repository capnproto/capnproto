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

#include "QtEventPort.hpp"

#include <QGuiApplication>
#include <QTimer>

QtEventPort::~QtEventPort()
{}

bool QtEventPort::wait() {
    // If events are already pending, they will be processed and we return immediately thereafter
    // Otherwise, we block until new events arrive
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
    return false;
}

bool QtEventPort::poll() {
    // Process any pending events, but don't block
    QCoreApplication::processEvents();
    return false;
}

void QtEventPort::setRunnable(bool runnable) {
    isRunnable = runnable;

    if(runnable)
        // As per Qt docs, this will schedule run() to be called when all currently pending events have been processed.
        QTimer::singleShot(0, this, SLOT(run()));
}

void QtEventPort::run() {
    if (kjLoop)
        kjLoop->run();

    if (isRunnable)
        // Still runnable? OK, but wait your turn
        QTimer::singleShot(0, this, SLOT(run()));
}
