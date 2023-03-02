// Copyright (c) 2023 Cloudflare, Inc. and contributors
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

#include "common.h"
#include "debug.h"
#include "memory.h"

KJ_BEGIN_HEADER

namespace kj {

  template<typename T>
  struct Node {
    // A node in a doubly linked-list.
    //
    // Node owns its `data`, so deallocating the Node deallocates the data (unless moved elsewhere).
    // The next Node in the linked-list is owned by `this` Node through its `next` property.
    Node(kj::Own<T> data): data(kj::mv(data)) {};
    ~Node() noexcept(false) {
      // Any links must have been correctly transferred prior to the destruction of this Node.
      // This assures us that we haven't accidentally deallocated any subsequent Node's in the list.
      KJ_REQUIRE(next == nullptr);
      KJ_REQUIRE(prev == nullptr);
    }

    bool operator==(Node<T>& other) {
      // This comparison only checks that `data` and `next` are the same, since they're wrapped in
      // `Owns`. We intentionally do not check `prev`, since that would trigger a recursive call
      // from `this` all the way up to the `head` node of the list.
      //
      // This means a node is distinguished by its `data` and `next` pointers.
      auto sameData = other.data == data;
      if ((next == nullptr && other.next == nullptr) && sameData) {
        return true;
      }
      // We want to ensure this and other point to the same `next`.
      KJ_IF_MAYBE(n, next) {
        KJ_IF_MAYBE(oN, other.next) {
          return ((*n == *oN) && sameData);
        }
      }
      return false;
    }

    void addNext(kj::Own<Node<T>> newNode) {
      // Adds newNode after `this`, handling both `prev` and `next` links.
      KJ_REQUIRE(!this->hasNext());

      next.emplace(kj::mv(newNode));
      auto& nextRef = *KJ_REQUIRE_NONNULL(next).get();
      nextRef.prev = this;
    }

    bool hasNext() {
      return next != nullptr;
    }

    bool hasPrev() {
      return prev != nullptr;
    }

    kj::Maybe<kj::Own<Node<T>>> next;
    // `this` node owns the the next node via `next`, so if we modify `next` without moving the
    // existing `next` node, then the next node (and all following nodes) will be deallocated!
    kj::Maybe<Node<T>&> prev;
    kj::Own<T> data;
  };

  template <typename T, typename MaybeConstT>
  class LinkedListIterator;

  template<typename T>
  class LinkedList {
    // A Doubly Linked-List consisting of Own<Node<T>>s.
    //    - Each Node owns the next Node; the first Node is owned by `head`.
    //
    // Always use kj::List over kj::LinkedList when possible.
    //    - If your linked-list must own its data (i.e. do memory allocation),
    //      then you might want to use kj::LinkedList instead.
  public:
    LinkedList() = default;
    ~LinkedList() noexcept {
      clear();
      KJ_ASSERT(size() == 0 && empty());
    }

    KJ_DISALLOW_COPY_AND_MOVE(LinkedList);

    bool empty() {
      return head == nullptr;
    }

    size_t size() {
      return listSize;
    }

    void clear() {
      // Removes all Nodes in the list.
      while (head != nullptr) {
        remove(*KJ_REQUIRE_NONNULL(head).get());
      }
    }

    void insertFront(kj::Own<Node<T>> newNode) {
      // Add newNode to the front of the doubly linked list.
      KJ_REQUIRE(!newNode->hasPrev());
      KJ_REQUIRE(!newNode->hasNext());

      KJ_IF_MAYBE(h, head) {
        // We have a node at the front.
        newNode->addNext(kj::mv(*h));
      } else {
        // The linked list is empty, let's set the head and tail to the same thing.
        tail = *newNode.get();
      }
      head.emplace(kj::mv(newNode));
      listSize++;
    }

    void insertLast(kj::Own<Node<T>> newNode) {
      // Add newNode to the tail of the doubly linked list.
      if (empty()) {
        return insertFront(kj::mv(newNode));
      }
      KJ_REQUIRE(!newNode->hasPrev());
      KJ_REQUIRE(!newNode->hasNext());

      auto& tailRef = KJ_REQUIRE_NONNULL(tail);
      tailRef.addNext(kj::mv(newNode));
      tail = tailRef.next;
      listSize++;
    }

    Node<T>& addFront(kj::Own<T> data) {
      // Allocates a new Node and consumes `data`.
      // The Node is added to the front of the list, and a reference to the Node is returned.
      auto node = kj::heap<Node<T>>(kj::mv(data));
      auto& refToNode = *node.get();
      this->insertFront(kj::mv(node));
      return refToNode;
    }

    Node<T>& addBack(kj::Own<T> data) {
      auto node = kj::heap<Node<T>>(kj::mv(data));
      auto& refToNode = *node.get();
      this->insertLast(kj::mv(node));
      return refToNode;
    }

    void remove(Node<T>& node) {
      KJ_REQUIRE(!empty() && size() != 0, "tried to remove from LinkedList when it was empty.");

      auto& headRef = *KJ_REQUIRE_NONNULL(head).get();
      auto& tailRef = KJ_REQUIRE_NONNULL(tail);

      if (size() == 1) {
        // Case 1: We are removing the only node.
        head = nullptr;
        tail = nullptr;
      } else if (headRef == node) {
        // Case 2: We are removing the head node.
        KJ_REQUIRE(headRef.prev == nullptr);

        auto& newHead = KJ_REQUIRE_NONNULL(headRef.next);
        newHead->prev = nullptr;
        head.emplace(kj::mv(newHead));
     } else if (tailRef == node) {
        // Case 3: We are removing the tail node.
        KJ_REQUIRE(tailRef.next == nullptr);

        auto& newTail = KJ_REQUIRE_NONNULL(tailRef.prev);
        tailRef.prev = nullptr;
        newTail.next = nullptr;
        tail = newTail;
      } else {
        // Case 4: We are removing some node in-between the head and the tail.
        auto& prev = KJ_REQUIRE_NONNULL(node.prev);
        auto& next = KJ_REQUIRE_NONNULL(node.next);

        // We have to modify the `prev` pointers first, since the `next` pointers own the nodes.
        next->prev = prev;
        node.prev = nullptr;
        prev.next.emplace(kj::mv(next));
      }
      listSize--;
    }

    Node<T>& getIndex(size_t index) {
      KJ_REQUIRE(index < size(), "tried to get index greater than list size!");

      // Trivial access cases:
      if (index == 0) {
        return *KJ_REQUIRE_NONNULL(head).get();
      } else if (index == size() - 1) {
        return KJ_REQUIRE_NONNULL(tail);
      }

      size_t count = 0;
      auto& headRef = KJ_REQUIRE_NONNULL(head);
      Node<T>* curr = headRef;

      while ((count++) < index) {
        curr = KJ_REQUIRE_NONNULL(curr->next);
      }
      return *curr;
    }

    typedef LinkedListIterator<T, T> Iterator;
    typedef LinkedListIterator<T, const T> ConstIterator;

    Iterator begin() {
      return Iterator(head.map([](auto& val) -> Node<T>& { return *val.get(); })); }
    Iterator end() { return Iterator(nullptr); }

    ConstIterator begin() const {
      return ConstIterator(head.map([](auto& val) -> Node<const T>& { return *val.get(); })); }
    ConstIterator end() const { return ConstIterator(nullptr); }

    T& front() { return *begin(); }
    const T& front() const { return *begin(); }

  private:
    kj::Maybe<kj::Own<Node<T>>> head;
    kj::Maybe<Node<T>&> tail;
    size_t listSize = 0;
  };

  template<typename T, typename MaybeConstT>
  class LinkedListIterator {

    using NodeRef = Node<MaybeConstT>&;

  public:
    LinkedListIterator() = default;

    MaybeConstT& operator*() {
      return *KJ_REQUIRE_NONNULL(current, "tried to dereference end of list").data.get();
    }

    const T& operator*() const {
      return *KJ_REQUIRE_NONNULL(current, "tried to dereference end of list").data.get();
    }

    MaybeConstT* operator->() {
      return *KJ_REQUIRE_NONNULL(current, "tried to dereference end of list").data.get();
    }

    const T* operator->() const {
      return *KJ_REQUIRE_NONNULL(current, "tried to dereference end of list").data.get();
    }

    inline LinkedListIterator& operator++() {
      current = next;
      next = tryGetNextRef(current);
      return *this;
    }

    inline LinkedListIterator operator++(int) {
      LinkedListIterator result = *this;
      ++*this;
      return result;
    }

    inline bool operator==(const LinkedListIterator& other) const {
      return _::readMaybe(current) == _::readMaybe(other.current);
    }
    inline bool operator!=(const LinkedListIterator& other) const {
      return _::readMaybe(current) != _::readMaybe(other.current);
    }

  private:
    Maybe<NodeRef> current;
    Maybe<NodeRef> next;

    Maybe<NodeRef> tryGetNextRef(kj::Maybe<NodeRef> curr) {
      return curr.map([](NodeRef node) -> kj::Maybe<NodeRef> {
          return node.next.map([](auto& nextNode) -> NodeRef { return *nextNode.get(); });
              }).orDefault(nullptr);
    }
    explicit LinkedListIterator(Maybe<NodeRef> start)
        : current(start),
          next(tryGetNextRef(start)) {}

    friend class LinkedList<T>;
  };

} // namespace kj

KJ_END_HEADER
