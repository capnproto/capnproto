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

#include "kj/memory.h"
#include "linked-list.h"
#include "debug.h"
#include <cstddef>
#include <stdlib.h>
#include <kj/test.h>

namespace kj {
namespace {

void addElements(LinkedList<size_t>& list, size_t n) {
  for (size_t i = n; i > 0; i--) {
    auto ptr = kj::heap<size_t>(i);
    auto node = kj::heap<Node<size_t>>(kj::mv(ptr));
    list.insertFront(kj::mv(node));
  }
}

KJ_TEST("Add Elements to Doubly Linked-List") {
  LinkedList<size_t> list;
  KJ_ASSERT(list.empty(), "List wasn't empty!");
  KJ_ASSERT(list.size() == 0, "List size wasn't 0!");

  auto elementsToAdd = 10;
  for (size_t i = elementsToAdd; i > 0; i--) {
    auto ptr = kj::heap<size_t>(i);
    auto node = kj::heap<Node<size_t>>(kj::mv(ptr));
    list.insertFront(kj::mv(node));

    KJ_ASSERT(!list.empty(), "List was empty after adding an element!");
    auto expected = (elementsToAdd - (i - 1));
    KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
        list.size());
    KJ_ASSERT(*list.getIndex(0).data.get() == i, "The node's data was not what we expected");
  }
  KJ_ASSERT(list.size() == elementsToAdd, "List size incorrect, expected ", elementsToAdd,
      "but got", list.size());
}

KJ_TEST("Add Elements to Back of Doubly Linked-List") {
  LinkedList<size_t> list;

  auto elementsToAdd = 10;
  for (size_t i = 0; i < elementsToAdd; i++) {
    auto ptr = kj::heap<size_t>(i);
    auto node = kj::heap<Node<size_t>>(kj::mv(ptr));
    list.insertLast(kj::mv(node));

    KJ_ASSERT(!list.empty(), "List was empty after adding an element!");
    auto expected = i + 1;
    KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
        list.size());
    KJ_ASSERT(*list.getIndex(list.size() - 1).data.get() == i,
        "The node's data was not what we expected");
  }
  KJ_ASSERT(list.size() == elementsToAdd, "List size incorrect, expected ", elementsToAdd,
      "but got", list.size());
}

KJ_TEST("Add Elements to Back of Doubly Linked-List but the List Handles Allocations") {
  LinkedList<size_t> list;

  auto elementsToAdd = 10;
  for (size_t i = 0; i < elementsToAdd; i++) {
    auto& node = list.addBack(kj::heap<size_t>(i));

    KJ_ASSERT(!list.empty(), "List was empty after adding an element!");
    auto expected = i + 1;
    KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
        list.size());
    auto& match = list.getIndex(list.size() - 1);
    KJ_ASSERT(match == node,
        "The node we created during add() doesn't match the node at the index we inserted it in.");
  }
  KJ_ASSERT(list.size() == elementsToAdd, "List size incorrect, expected ", elementsToAdd,
      "but got", list.size());
}

KJ_TEST("Remove Head from Linked List with More Than 1 Element") {
  LinkedList<size_t> list;

  // Node data will be 1 -> 2 -> 3 -> 4
  auto elementsToAdd = 4;
  addElements(list, elementsToAdd);
  auto& head = list.getIndex(0);
  list.remove(head);

  // After removing head, node data will be 2 -> 3 -> 4
  auto expected = elementsToAdd - 1;
  KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
      list.size());
  KJ_ASSERT(*list.getIndex(0).data.get() == 2);
}

KJ_TEST("Remove Tail from Linked List with More Than 1 element") {
  LinkedList<size_t> list;

  // Node data will be 1 -> 2 -> 3 -> 4
  auto elementsToAdd = 4;
  addElements(list, elementsToAdd);

  auto& tail = list.getIndex(list.size() - 1);
  list.remove(tail);

  // After removing tail, node data will be 1 -> 2 -> 3
  auto expected = elementsToAdd - 1;
  KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
      list.size());
  KJ_ASSERT(*list.getIndex(list.size() - 1).data.get() == 3);
}

KJ_TEST("Remove Head from Linked List with 1 Element") {
  LinkedList<size_t> list;

  // Node data will be 1 -> null
  auto elementsToAdd = 1;
  addElements(list, elementsToAdd);

  auto& head = list.getIndex(0);
  list.remove(head);

  // After removing head, list is empty
  auto expected = elementsToAdd - 1;
  KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
      list.size());
  KJ_ASSERT(list.empty(), "List was not empty after deleting final element?");
}

KJ_TEST("Remove Tail from Linked List with 1 Element") {
  LinkedList<size_t> list;

  // Node data will be 1 -> null
  auto elementsToAdd = 1;
  addElements(list, elementsToAdd);

  auto& tail = list.getIndex(0);
  list.remove(tail);

  // After removing tail, list is empty
  auto expected = elementsToAdd - 1;
  KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
      list.size());
  KJ_ASSERT(list.empty(), "List was not empty after deleting final element?");
}

KJ_TEST("Remove a Middle Node from Linked List") {
  LinkedList<size_t> list;

  // Node data will be 1 -> 2 -> 3 -> 4
  // We will remove the node with data == 2.
  auto elementsToAdd = 4;
  addElements(list, elementsToAdd);

  auto& head = list.getIndex(0);
  auto& nodeToRemove = list.getIndex(1);
  list.remove(nodeToRemove);

  auto expected = elementsToAdd - 1;
  KJ_ASSERT(list.size() == expected, "List size incorrect, expected ", expected, "but got",
      list.size());
  auto& nextExpected = list.getIndex(1);
  // We now expect to have 1 -> 3 -> 4
  KJ_ASSERT(*nextExpected.data.get() == 3);
  // We expect Node with data 3 to have prev pointer to head.
  KJ_ASSERT(KJ_REQUIRE_NONNULL(nextExpected.prev) == head);
}

KJ_TEST("Remove Random Nodes from the Linked List until it's Empty") {
  LinkedList<size_t> list;
  srand(0);

  auto elementsToAdd = 5000;
  addElements(list, elementsToAdd);

  auto size = list.size();
  while (!list.empty()) {
    auto index = rand() % list.size();
    auto& node = list.getIndex(index);
    list.remove(node);
    KJ_ASSERT(list.size() == (size--) - 1);
  }
}

KJ_TEST("Iterate Over Elements") {
  LinkedList<size_t> list;

  auto elementsToAdd = 10000;
  addElements(list, elementsToAdd);

  auto counter = 1;
  for (auto& e: list) {
    KJ_ASSERT(e == counter++, "got unexpected value while iterating LinkedList");
  }
}

KJ_TEST("Const Iterate Over Elements") {
  LinkedList<size_t> list;

  auto elementsToAdd = 10000;
  addElements(list, elementsToAdd);

  auto counter = 1;
  for (const auto& e: list) {
    KJ_ASSERT(e == counter++, "got unexpected value while iterating LinkedList");
  }
}
}  // namespace
}  // namespace kj
