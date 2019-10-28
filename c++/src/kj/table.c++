// Copyright (c) 2018 Kenton Varda and contributors
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

#include "table.h"
#include "debug.h"
#include <stdlib.h>

namespace kj {
namespace _ {

static inline uint lg(uint value) {
  // Compute floor(log2(value)).
  //
  // Undefined for value = 0.
#if _MSC_VER
  unsigned long i;
  auto found = _BitScanReverse(&i, value);
  KJ_DASSERT(found);  // !found means value = 0
  return i;
#else
  return sizeof(uint) * 8 - 1 - __builtin_clz(value);
#endif
}

void throwDuplicateTableRow() {
  KJ_FAIL_REQUIRE("inserted row already exists in table");
}

void logHashTableInconsistency() {
  KJ_LOG(ERROR,
      "HashIndex detected hash table inconsistency. This can happen if you create a kj::Table "
      "with a hash index and you modify the rows in the table post-indexing in a way that would "
      "change their hash. This is a serious bug which will lead to undefined behavior."
      "\nstack: ", kj::getStackTrace());
}

// List of primes where each element is roughly double the previous.  Obtained
// from:
//   http://planetmath.org/goodhashtableprimes
// Primes < 53 were added to ensure that small tables don't allocate excessive memory.
static const size_t PRIMES[] = {
           1,  // 2^ 0 = 1
           3,  // 2^ 1 = 2
           5,  // 2^ 2 = 4
          11,  // 2^ 3 = 8
          23,  // 2^ 4 = 16
          53,  // 2^ 5 = 32
          97,  // 2^ 6 = 64
         193,  // 2^ 7 = 128
         389,  // 2^ 8 = 256
         769,  // 2^ 9 = 512
        1543,  // 2^10 = 1024
        3079,  // 2^11 = 2048
        6151,  // 2^12 = 4096
       12289,  // 2^13 = 8192
       24593,  // 2^14 = 16384
       49157,  // 2^15 = 32768
       98317,  // 2^16 = 65536
      196613,  // 2^17 = 131072
      393241,  // 2^18 = 262144
      786433,  // 2^19 = 524288
     1572869,  // 2^20 = 1048576
     3145739,  // 2^21 = 2097152
     6291469,  // 2^22 = 4194304
    12582917,  // 2^23 = 8388608
    25165843,  // 2^24 = 16777216
    50331653,  // 2^25 = 33554432
   100663319,  // 2^26 = 67108864
   201326611,  // 2^27 = 134217728
   402653189,  // 2^28 = 268435456
   805306457,  // 2^29 = 536870912
  1610612741,  // 2^30 = 1073741824
};

uint chooseBucket(uint hash, uint count) {
  // Integer modulus is really, really slow. It turns out that the compiler can generate much
  // faster code if the denominator is a constant. Since we have a fixed set of possible
  // denominators, a big old switch() statement is a win.

  // TODO(perf): Consider using power-of-two bucket sizes. We can safely do so as long as we demand
  //   high-quality hash functions -- kj::hashCode() needs good diffusion even for integers, can't
  //   just be a cast. Also be sure to implement Robin Hood hashing to avoid extremely bad negative
  //   lookup time when elements have sequential hashes (otherwise, it could be necessary to scan
  //   the entire list to determine that an element isn't present).

  switch (count) {
#define HANDLE(i) case i##u: return hash % i##u
    HANDLE(         1);
    HANDLE(         3);
    HANDLE(         5);
    HANDLE(        11);
    HANDLE(        23);
    HANDLE(        53);
    HANDLE(        97);
    HANDLE(       193);
    HANDLE(       389);
    HANDLE(       769);
    HANDLE(      1543);
    HANDLE(      3079);
    HANDLE(      6151);
    HANDLE(     12289);
    HANDLE(     24593);
    HANDLE(     49157);
    HANDLE(     98317);
    HANDLE(    196613);
    HANDLE(    393241);
    HANDLE(    786433);
    HANDLE(   1572869);
    HANDLE(   3145739);
    HANDLE(   6291469);
    HANDLE(  12582917);
    HANDLE(  25165843);
    HANDLE(  50331653);
    HANDLE( 100663319);
    HANDLE( 201326611);
    HANDLE( 402653189);
    HANDLE( 805306457);
    HANDLE(1610612741);
#undef HANDLE
    default: return hash % count;
  }
}

size_t chooseHashTableSize(uint size) {
  if (size == 0) return 0;

  // Add 1 to compensate for the floor() above, then look up the best prime bucket size for that
  // target size.
  return PRIMES[lg(size) + 1];
}

kj::Array<HashBucket> rehash(kj::ArrayPtr<const HashBucket> oldBuckets, size_t targetSize) {
  // Rehash the whole table.

  KJ_REQUIRE(targetSize < (1 << 30), "hash table has reached maximum size");

  size_t size = chooseHashTableSize(targetSize);

  if (size < oldBuckets.size()) {
    size = oldBuckets.size();
  }

  auto newBuckets = kj::heapArray<HashBucket>(size);
  memset(newBuckets.begin(), 0, sizeof(HashBucket) * size);

  uint entryCount = 0;
  uint collisionCount = 0;

  for (auto& oldBucket: oldBuckets) {
    if (oldBucket.isOccupied()) {
      ++entryCount;
      for (uint i = oldBucket.hash % newBuckets.size();; i = probeHash(newBuckets, i)) {
        auto& newBucket = newBuckets[i];
        if (newBucket.isEmpty()) {
          newBucket = oldBucket;
          break;
        }
        ++collisionCount;
      }
    }
  }

  if (collisionCount > 16 + entryCount * 4) {
    static bool warned = false;
    if (!warned) {
      KJ_LOG(WARNING, "detected excessive collisions in hash table; is your hash function OK?",
          entryCount, collisionCount, kj::getStackTrace());
      warned = true;
    }
  }

  return newBuckets;
}

// =======================================================================================
// BTree

#if _WIN32
#define aligned_free _aligned_free
#else
#define aligned_free ::free
#endif

BTreeImpl::BTreeImpl()
    : tree(const_cast<NodeUnion*>(&EMPTY_NODE)),
      treeCapacity(1),
      height(0),
      freelistHead(1),
      freelistSize(0),
      beginLeaf(0),
      endLeaf(0) {}
BTreeImpl::~BTreeImpl() noexcept(false) {
  if (tree != &EMPTY_NODE) {
    aligned_free(tree);
  }
}

const BTreeImpl::NodeUnion BTreeImpl::EMPTY_NODE = {{{0, {0}}}};

void BTreeImpl::verify(size_t size, FunctionParam<bool(uint, uint)> f) {
  KJ_ASSERT(verifyNode(size, f, 0, height, nullptr) == size);
}
size_t BTreeImpl::verifyNode(size_t size, FunctionParam<bool(uint, uint)>& f,
                             uint pos, uint height, MaybeUint maxRow) {
  if (height > 0) {
    auto& parent = tree[pos].parent;

    auto n = parent.keyCount();
    size_t total = 0;
    for (auto i: kj::zeroTo(n)) {
      KJ_ASSERT(*parent.keys[i] < size);
      total += verifyNode(size, f, parent.children[i], height - 1, parent.keys[i]);
      KJ_ASSERT(i + 1 == n || f(*parent.keys[i], *parent.keys[i + 1]));
    }
    total += verifyNode(size, f, parent.children[n], height - 1, maxRow);
    KJ_ASSERT(maxRow == nullptr || f(*parent.keys[n-1], *maxRow));
    return total;
  } else {
    auto& leaf = tree[pos].leaf;
    auto n = leaf.size();
    for (auto i: kj::zeroTo(n)) {
      KJ_ASSERT(*leaf.rows[i] < size);
      if (i + 1 < n) {
        KJ_ASSERT(f(*leaf.rows[i], *leaf.rows[i + 1]));
      } else {
        KJ_ASSERT(maxRow == nullptr || leaf.rows[n-1] == maxRow);
      }
    }
    return n;
  }
}

void BTreeImpl::logInconsistency() const {
  KJ_LOG(ERROR,
      "BTreeIndex detected tree state inconsistency. This can happen if you create a kj::Table "
      "with a b-tree index and you modify the rows in the table post-indexing in a way that would "
      "change their ordering. This is a serious bug which will lead to undefined behavior."
      "\nstack: ", kj::getStackTrace());
}

void BTreeImpl::reserve(size_t size) {
  KJ_REQUIRE(size < (1u << 31), "b-tree has reached maximum size");

  // Calculate the worst-case number of leaves to cover the size, given that a leaf is always at
  // least half-full. (Note that it's correct for this calculation to round down, not up: The
  // remainder will necessarily be distributed among the non-full leaves, rather than creating a
  // new leaf, because if it went into a new leaf, that leaf would be less than half-full.)
  uint leaves = size / (Leaf::NROWS / 2);

  // Calculate the worst-case number of parents to cover the leaves, given that a parent is always
  // at least half-full. Since the parents form a tree with branching factor B, the size of the
  // tree is N/B + N/B^2 + N/B^3 + N/B^4 + ... = N / (B - 1). Math.
  constexpr uint branchingFactor = Parent::NCHILDREN / 2;
  uint parents = leaves / (branchingFactor - 1);

  // Height is log-base-branching-factor of leaves, plus 1 for the root node.
  uint height = lg(leaves | 1) / lg(branchingFactor) + 1;

  size_t newSize = leaves +
      parents + 1 +  // + 1 for the root
      height + 2;    // minimum freelist size needed by insert()

  if (treeCapacity < newSize) {
    growTree(newSize);
  }
}

void BTreeImpl::clear() {
  if (tree != &EMPTY_NODE) {
    azero(tree, treeCapacity);
    height = 0;
    freelistHead = 1;
    freelistSize = treeCapacity;
    beginLeaf = 0;
    endLeaf = 0;
  }
}

void BTreeImpl::growTree(uint minCapacity) {
  uint newCapacity = kj::max(kj::max(minCapacity, treeCapacity * 2), 4);
  freelistSize += newCapacity - treeCapacity;

  // Allocate some aligned memory! In theory this should be as simple as calling the C11 standard
  // aligned_alloc() function. Unfortunately, many platforms don't implement it. Luckily, there
  // are usually alternatives.

#if __APPLE__ || __BIONIC__ || __OpenBSD__
  // macOS, OpenBSD, and Android lack aligned_alloc(), but have posix_memalign(). Fine.
  void* allocPtr;
  int error = posix_memalign(&allocPtr,
      sizeof(BTreeImpl::NodeUnion), newCapacity * sizeof(BTreeImpl::NodeUnion));
  if (error != 0) {
    KJ_FAIL_SYSCALL("posix_memalign", error);
  }
  NodeUnion* newTree = reinterpret_cast<NodeUnion*>(allocPtr);
#elif _WIN32
  // Windows lacks aligned_alloc() but has its own _aligned_malloc() (which requires freeing using
  // _aligned_free()).
  // WATCH OUT: The argument order for _aligned_malloc() is opposite of aligned_alloc()!
  NodeUnion* newTree = reinterpret_cast<NodeUnion*>(
      _aligned_malloc(newCapacity * sizeof(BTreeImpl::NodeUnion), sizeof(BTreeImpl::NodeUnion)));
  KJ_ASSERT(newTree != nullptr, "memory allocation failed", newCapacity);
#else
  // Let's use the C11 standard.
  NodeUnion* newTree = reinterpret_cast<NodeUnion*>(
      aligned_alloc(sizeof(BTreeImpl::NodeUnion), newCapacity * sizeof(BTreeImpl::NodeUnion)));
  KJ_ASSERT(newTree != nullptr, "memory allocation failed", newCapacity);
#endif

  acopy(newTree, tree, treeCapacity);
  azero(newTree + treeCapacity, newCapacity - treeCapacity);
  if (tree != &EMPTY_NODE) aligned_free(tree);
  tree = newTree;
  treeCapacity = newCapacity;
}

BTreeImpl::Iterator BTreeImpl::search(const SearchKey& searchKey) const {
  // Find the "first" row number (in sorted order) for which searchKey.isAfter(rowNumber) returns
  // false.

  uint pos = 0;

  for (auto i KJ_UNUSED: zeroTo(height)) {
    auto& parent = tree[pos].parent;
    pos = parent.children[searchKey.search(parent)];
  }

  auto& leaf = tree[pos].leaf;
  return { tree, &leaf, searchKey.search(leaf) };
}

template <typename T>
struct BTreeImpl::AllocResult {
  uint index;
  T& node;
};

template <typename T>
inline BTreeImpl::AllocResult<T> BTreeImpl::alloc() {
  // Allocate a new item from the freelist. Guaranteed to be zero'd except for the first member.
  uint i = freelistHead;
  NodeUnion* ptr = &tree[i];
  freelistHead = i + 1 + ptr->freelist.nextOffset;
  --freelistSize;
  return { i, *ptr };
}

inline void BTreeImpl::free(uint pos) {
  // Add the given node to the freelist.

  // HACK: This is typically called on a node immediately after copying its contents away, but the
  //   pointer used to copy it away may be a different pointer pointing to a different union member
  //   which the compiler may not recgonize as aliasing with this object. Just to be extra-safe,
  //   insert a compiler barrier.
  compilerBarrier();

  auto& node = tree[pos];
  node.freelist.nextOffset = freelistHead - pos - 1;
  azero(node.freelist.zero, kj::size(node.freelist.zero));
  freelistHead = pos;
  ++freelistSize;
}

BTreeImpl::Iterator BTreeImpl::insert(const SearchKey& searchKey) {
  // Like search() but ensures that there is room in the leaf node to insert a new row.

  // If we split the root node it will generate two new nodes. If we split any other node in the
  // path it will generate one new node. `height` doesn't count leaf nodes, but we can equivalently
  // think of it as not counting the root node, so in the worst case we may allocate height + 2
  // new nodes.
  //
  // (Also note that if the tree is currently empty, then `tree` points to a dummy root node in
  // read-only memory. We definitely need to allocate a real tree node array in this case, and
  // we'll start out allocating space for four nodes, which will be all we need up to 28 rows.)
  if (freelistSize < height + 2) {
    if (height > 0 && !tree[0].parent.isFull() && freelistSize >= height) {
      // Slight optimization: The root node is not full, so we're definitely not going to split it.
      // That means that the maximum allocations we might do is equal to `height`, not
      // `height + 2`, and we have that much space, so no need to grow yet.
      //
      // This optimization is particularly important for small trees, e.g. when treeCapacity is 4
      // and the tree so far consists of a root and two children, we definitely don't need to grow
      // the tree yet.
    } else {
      growTree();

      if (freelistHead == 0) {
        // We have no root yet. Allocate one.
        KJ_ASSERT(alloc<Parent>().index == 0);
      }
    }
  }

  uint pos = 0;

  // Track grandparent node and child index within grandparent.
  Parent* parent = nullptr;
  uint indexInParent = 0;

  for (auto i KJ_UNUSED: zeroTo(height)) {
    Parent& node = insertHelper(searchKey, tree[pos].parent, parent, indexInParent, pos);

    parent = &node;
    indexInParent = searchKey.search(node);
    pos = node.children[indexInParent];
  }

  Leaf& leaf = insertHelper(searchKey, tree[pos].leaf, parent, indexInParent, pos);

  // Fun fact: Unlike erase(), there's no need to climb back up the tree modifying keys, because
  // either the newly-inserted node will not be the last in the leaf (and thus parent keys aren't
  // modified), or the leaf is the last leaf in the tree (and thus there's no parent key to
  // modify).

  return { tree, &leaf, searchKey.search(leaf) };
}

template <typename Node>
Node& BTreeImpl::insertHelper(const SearchKey& searchKey,
    Node& node, Parent* parent, uint indexInParent, uint pos) {
  if (node.isFull()) {
    // This node is full. Need to split.
    if (parent == nullptr) {
      // This is the root node. We need to split into two nodes and create a new root.
      auto n1 = alloc<Node>();
      auto n2 = alloc<Node>();

      uint pivot = split(n2.node, n2.index, node, pos);
      move(n1.node, n1.index, node);

      // Rewrite root to have the two children.
      tree[0].parent.initRoot(pivot, n1.index, n2.index);

      // Increased height.
      ++height;

      // Decide which new branch has our search key.
      if (searchKey.isAfter(pivot)) {
        // the right one
        return n2.node;
      } else {
        // the left one
        return n1.node;
      }
    } else {
      // This is a non-root parent node. We need to split it into two and insert the new node
      // into the grandparent.
      auto n = alloc<Node>();
      uint pivot = split(n.node, n.index, node, pos);

      // Insert new child into grandparent.
      parent->insertAfter(indexInParent, pivot, n.index);

      // Decide which new branch has our search key.
      if (searchKey.isAfter(pivot)) {
        // the new one, which is right of the original
        return n.node;
      } else {
        // the original one, which is left of the new one
        return node;
      }
    }
  } else {
    // No split needed.
    return node;
  }
}

void BTreeImpl::erase(uint row, const SearchKey& searchKey) {
  // Erase the given row number from the tree. predicate() returns true for the given row and all
  // rows after it.

  uint pos = 0;

  // Track grandparent node and child index within grandparent.
  Parent* parent = nullptr;
  uint indexInParent = 0;

  MaybeUint* fixup = nullptr;

  for (auto i KJ_UNUSED: zeroTo(height)) {
    Parent& node = eraseHelper(tree[pos].parent, parent, indexInParent, pos, fixup);

    parent = &node;
    indexInParent = searchKey.search(node);
    pos = node.children[indexInParent];

    if (indexInParent < kj::size(node.keys) && node.keys[indexInParent] == row) {
      // Oh look, the row is a key in this node! We'll need to come back and fix this up later.
      // Note that any particular row can only appear as *one* key value anywhere in the tree, so
      // we only need one fixup pointer, which is nice.
      MaybeUint* newFixup = &node.keys[indexInParent];
      if (fixup == newFixup) {
        // The fixup pointer was already set while processing a parent node, and then a merge or
        // rotate caused it to be moved, but the fixup pointer was updated... so it's already set
        // to point at the slot we wanted it to point to, so nothing to see here.
      } else {
        KJ_DASSERT(fixup == nullptr);
        fixup = newFixup;
      }
    }
  }

  Leaf& leaf = eraseHelper(tree[pos].leaf, parent, indexInParent, pos, fixup);

  uint r = searchKey.search(leaf);
  if (leaf.rows[r] == row) {
    leaf.erase(r);

    if (fixup != nullptr) {
      // There's a key in a parent node that needs fixup. This is only possible if the removed
      // node is the last in its leaf.
      KJ_DASSERT(leaf.rows[r] == nullptr);
      KJ_DASSERT(r > 0);  // non-root nodes must be at least half full so this can't be item 0
      KJ_DASSERT(*fixup == row);
      *fixup = leaf.rows[r - 1];
    }
  } else {
    logInconsistency();
  }
}

template <typename Node>
Node& BTreeImpl::eraseHelper(
    Node& node, Parent* parent, uint indexInParent, uint pos, MaybeUint*& fixup) {
  if (parent != nullptr && !node.isMostlyFull()) {
    // This is not the root, but it's only half-full. Rebalance.
    KJ_DASSERT(node.isHalfFull());

    if (indexInParent > 0) {
      // There's a sibling to the left.
      uint sibPos = parent->children[indexInParent - 1];
      Node& sib = tree[sibPos];
      if (sib.isMostlyFull()) {
        // Left sibling is more than half full. Steal one member.
        rotateRight(sib, node, *parent, indexInParent - 1);
        return node;
      } else {
        // Left sibling is half full, too. Merge.
        KJ_ASSERT(sib.isHalfFull());
        merge(sib, sibPos, *parent->keys[indexInParent - 1], node);
        parent->eraseAfter(indexInParent - 1);
        free(pos);
        if (fixup == &parent->keys[indexInParent]) --fixup;

        if (parent->keys[0] == nullptr) {
          // Oh hah, the parent has no keys left. It must be the root. We can eliminate it.
          KJ_DASSERT(parent == &tree->parent);
          compilerBarrier();  // don't reorder any writes to parent below here
          move(tree[0], 0, sib);
          free(sibPos);
          --height;
          return tree[0];
        } else {
          return sib;
        }
      }
    } else if (indexInParent < Parent::NKEYS && parent->keys[indexInParent] != nullptr) {
      // There's a sibling to the right.
      uint sibPos = parent->children[indexInParent + 1];
      Node& sib = tree[sibPos];
      if (sib.isMostlyFull()) {
        // Right sibling is more than half full. Steal one member.
        rotateLeft(node, sib, *parent, indexInParent, fixup);
        return node;
      } else {
        // Right sibling is half full, too. Merge.
        KJ_ASSERT(sib.isHalfFull());
        merge(node, pos, *parent->keys[indexInParent], sib);
        parent->eraseAfter(indexInParent);
        free(sibPos);
        if (fixup == &parent->keys[indexInParent]) fixup = nullptr;

        if (parent->keys[0] == nullptr) {
          // Oh hah, the parent has no keys left. It must be the root. We can eliminate it.
          KJ_DASSERT(parent == &tree->parent);
          compilerBarrier();  // don't reorder any writes to parent below here
          move(tree[0], 0, node);
          free(pos);
          --height;
          return tree[0];
        } else {
          return node;
        }
      }
    } else {
      KJ_FAIL_ASSERT("inconsistent b-tree");
    }
  }

  return node;
}

void BTreeImpl::renumber(uint oldRow, uint newRow, const SearchKey& searchKey) {
  // Renumber the given row from oldRow to newRow. predicate() returns true for oldRow and all
  // rows after it. (It will not be called on newRow.)

  uint pos = 0;

  for (auto i KJ_UNUSED: zeroTo(height)) {
    auto& node = tree[pos].parent;
    uint indexInParent = searchKey.search(node);
    pos = node.children[indexInParent];
    if (node.keys[indexInParent] == oldRow) {
      node.keys[indexInParent] = newRow;
    }
    KJ_DASSERT(pos != 0);
  }

  auto& leaf = tree[pos].leaf;
  uint r = searchKey.search(leaf);
  if (leaf.rows[r] == oldRow) {
    leaf.rows[r] = newRow;
  } else {
    logInconsistency();
  }
}

uint BTreeImpl::split(Parent& dst, uint dstPos, Parent& src, uint srcPos) {
  constexpr size_t mid = Parent::NKEYS / 2;
  uint pivot = *src.keys[mid];
  acopy(dst.keys, src.keys + mid + 1, Parent::NKEYS - mid - 1);
  azero(src.keys + mid, Parent::NKEYS - mid);
  acopy(dst.children, src.children + mid + 1, Parent::NCHILDREN - mid - 1);
  azero(src.children + mid + 1, Parent::NCHILDREN - mid - 1);
  return pivot;
}

uint BTreeImpl::split(Leaf& dst, uint dstPos, Leaf& src, uint srcPos) {
  constexpr size_t mid = Leaf::NROWS / 2;
  uint pivot = *src.rows[mid - 1];
  acopy(dst.rows, src.rows + mid, Leaf::NROWS - mid);
  azero(src.rows + mid, Leaf::NROWS - mid);

  if (src.next == 0) {
    endLeaf = dstPos;
  } else {
    tree[src.next].leaf.prev = dstPos;
  }
  dst.next = src.next;
  dst.prev = srcPos;
  src.next = dstPos;

  return pivot;
}

void BTreeImpl::merge(Parent& dst, uint dstPos, uint pivot, Parent& src) {
  // merge() is only legal if both nodes are half-empty. Meanwhile, B-tree invariants
  // guarantee that the node can't be more than half-empty, or we would have merged it sooner.
  // (The root can be more than half-empty, but it is never merged with anything.)
  KJ_DASSERT(src.isHalfFull());
  KJ_DASSERT(dst.isHalfFull());

  constexpr size_t mid = Parent::NKEYS/2;
  dst.keys[mid] = pivot;
  acopy(dst.keys + mid + 1, src.keys, mid);
  acopy(dst.children + mid + 1, src.children, mid + 1);
}

void BTreeImpl::merge(Leaf& dst, uint dstPos, uint pivot, Leaf& src) {
  // merge() is only legal if both nodes are half-empty. Meanwhile, B-tree invariants
  // guarantee that the node can't be more than half-empty, or we would have merged it sooner.
  // (The root can be more than half-empty, but it is never merged with anything.)
  KJ_DASSERT(src.isHalfFull());
  KJ_DASSERT(dst.isHalfFull());

  constexpr size_t mid = Leaf::NROWS/2;
  dst.rows[mid] = pivot;
  acopy(dst.rows + mid, src.rows, mid);

  dst.next = src.next;
  if (dst.next == 0) {
    endLeaf = dstPos;
  } else {
    tree[dst.next].leaf.prev = dstPos;
  }
}

void BTreeImpl::move(Parent& dst, uint dstPos, Parent& src) {
  dst = src;
}

void BTreeImpl::move(Leaf& dst, uint dstPos, Leaf& src) {
  dst = src;
  if (src.next == 0) {
    endLeaf = dstPos;
  } else {
    tree[src.next].leaf.prev = dstPos;
  }
  if (src.prev == 0) {
    beginLeaf = dstPos;
  } else {
    tree[src.prev].leaf.next = dstPos;
  }
}

void BTreeImpl::rotateLeft(
    Parent& left, Parent& right, Parent& parent, uint indexInParent, MaybeUint*& fixup) {
  // Steal one item from the right node and move it to the left node.

  // Like merge(), this is only called on an exactly-half-empty node.
  KJ_DASSERT(left.isHalfFull());
  KJ_DASSERT(right.isMostlyFull());

  constexpr size_t mid = Parent::NKEYS/2;
  left.keys[mid] = parent.keys[indexInParent];
  if (fixup == &parent.keys[indexInParent]) fixup = &left.keys[mid];
  parent.keys[indexInParent] = right.keys[0];
  left.children[mid + 1] = right.children[0];
  amove(right.keys, right.keys + 1, Parent::NKEYS - 1);
  right.keys[Parent::NKEYS - 1] = nullptr;
  amove(right.children, right.children + 1, Parent::NCHILDREN - 1);
  right.children[Parent::NCHILDREN - 1] = 0;
}

void BTreeImpl::rotateLeft(
    Leaf& left, Leaf& right, Parent& parent, uint indexInParent, MaybeUint*& fixup) {
  // Steal one item from the right node and move it to the left node.

  // Like merge(), this is only called on an exactly-half-empty node.
  KJ_DASSERT(left.isHalfFull());
  KJ_DASSERT(right.isMostlyFull());

  constexpr size_t mid = Leaf::NROWS/2;
  parent.keys[indexInParent] = left.rows[mid] = right.rows[0];
  if (fixup == &parent.keys[indexInParent]) fixup = nullptr;
  amove(right.rows, right.rows + 1, Leaf::NROWS - 1);
  right.rows[Leaf::NROWS - 1] = nullptr;
}

void BTreeImpl::rotateRight(Parent& left, Parent& right, Parent& parent, uint indexInParent) {
  // Steal one item from the left node and move it to the right node.

  // Like merge(), this is only called on an exactly-half-empty node.
  KJ_DASSERT(right.isHalfFull());
  KJ_DASSERT(left.isMostlyFull());

  constexpr size_t mid = Parent::NKEYS/2;
  amove(right.keys + 1, right.keys, mid);
  amove(right.children + 1, right.children, mid + 1);

  uint back = left.keyCount() - 1;

  right.keys[0] = parent.keys[indexInParent];
  parent.keys[indexInParent] = left.keys[back];
  right.children[0] = left.children[back + 1];
  left.keys[back] = nullptr;
  left.children[back + 1] = 0;
}

void BTreeImpl::rotateRight(Leaf& left, Leaf& right, Parent& parent, uint indexInParent) {
  // Steal one item from the left node and move it to the right node.

  // Like mergeFrom(), this is only called on an exactly-half-empty node.
  KJ_DASSERT(right.isHalfFull());
  KJ_DASSERT(left.isMostlyFull());

  constexpr size_t mid = Leaf::NROWS/2;
  amove(right.rows + 1, right.rows, mid);

  uint back = left.size() - 1;

  right.rows[0] = left.rows[back];
  parent.keys[indexInParent] = left.rows[back - 1];
  left.rows[back] = nullptr;
}

void BTreeImpl::Parent::initRoot(uint key, uint leftChild, uint rightChild) {
  // HACK: This is typically called on the root node immediately after copying its contents away,
  //   but the pointer used to copy it away may be a different pointer pointing to a different
  //   union member which the compiler may not recgonize as aliasing with this object. Just to
  //   be extra-safe, insert a compiler barrier.
  compilerBarrier();

  keys[0] = key;
  children[0] = leftChild;
  children[1] = rightChild;
  azero(keys + 1, Parent::NKEYS - 1);
  azero(children + 2, Parent::NCHILDREN - 2);
}

void BTreeImpl::Parent::insertAfter(uint i, uint splitKey, uint child) {
  KJ_IREQUIRE(children[Parent::NCHILDREN - 1] == 0);  // check not full

  amove(keys + i + 1, keys + i, Parent::NKEYS - (i + 1));
  keys[i] = splitKey;

  amove(children + i + 2, children + i + 1, Parent::NCHILDREN - (i + 2));
  children[i + 1] = child;
}

void BTreeImpl::Parent::eraseAfter(uint i) {
  amove(keys + i, keys + i + 1, Parent::NKEYS - (i + 1));
  keys[Parent::NKEYS - 1] = nullptr;
  amove(children + i + 1, children + i + 2, Parent::NCHILDREN - (i + 2));
  children[Parent::NCHILDREN - 1] = 0;
}

}  // namespace _

// =======================================================================================
// Insertion order

const InsertionOrderIndex::Link InsertionOrderIndex::EMPTY_LINK = { 0, 0 };

InsertionOrderIndex::InsertionOrderIndex(): capacity(0), links(const_cast<Link*>(&EMPTY_LINK)) {}
InsertionOrderIndex::InsertionOrderIndex(InsertionOrderIndex&& other)
    : capacity(other.capacity), links(other.links) {
  other.capacity = 0;
  other.links = const_cast<Link*>(&EMPTY_LINK);
}
InsertionOrderIndex& InsertionOrderIndex::operator=(InsertionOrderIndex&& other) {
  KJ_DASSERT(&other != this);
  capacity = other.capacity;
  links = other.links;
  other.capacity = 0;
  other.links = const_cast<Link*>(&EMPTY_LINK);
  return *this;
}
InsertionOrderIndex::~InsertionOrderIndex() noexcept(false) {
  if (links != &EMPTY_LINK) delete[] links;
}

void InsertionOrderIndex::reserve(size_t size) {
  KJ_ASSERT(size < (1u << 31), "Table too big for InsertionOrderIndex");

  if (size > capacity) {
    // Need to grow.
    // Note that `size` and `capacity` do not include the special link[0].

    // Round up to the next power of 2.
    size_t allocation = 1u << (_::lg(size) + 1);
    KJ_DASSERT(allocation > size);
    KJ_DASSERT(allocation <= size * 2);

    // Round first allocation up to 8.
    allocation = kj::max(allocation, 8);

    Link* newLinks = new Link[allocation];
#ifdef KJ_DEBUG
    // To catch bugs, fill unused links with 0xff.
    memset(newLinks, 0xff, allocation * sizeof(Link));
#endif
    _::acopy(newLinks, links, capacity + 1);
    if (links != &EMPTY_LINK) delete[] links;
    links = newLinks;
    capacity = allocation - 1;
  }
}

void InsertionOrderIndex::clear() {
  links[0] = Link { 0, 0 };

#ifdef KJ_DEBUG
  // To catch bugs, fill unused links with 0xff.
  memset(links + 1, 0xff, capacity * sizeof(Link));
#endif
}

kj::Maybe<size_t> InsertionOrderIndex::insertImpl(size_t pos) {
  if (pos >= capacity) {
    reserve(pos + 1);
  }

  links[pos + 1].prev = links[0].prev;
  links[pos + 1].next = 0;
  links[links[0].prev].next = pos + 1;
  links[0].prev = pos + 1;

  return nullptr;
}

void InsertionOrderIndex::eraseImpl(size_t pos) {
  Link& link = links[pos + 1];
  links[link.next].prev = link.prev;
  links[link.prev].next = link.next;

#ifdef KJ_DEBUG
  memset(&link, 0xff, sizeof(Link));
#endif
}

void InsertionOrderIndex::moveImpl(size_t oldPos, size_t newPos) {
  Link& link = links[oldPos + 1];
  Link& newLink = links[newPos + 1];

  newLink = link;

  KJ_DASSERT(links[link.next].prev == oldPos + 1);
  KJ_DASSERT(links[link.prev].next == oldPos + 1);
  links[link.next].prev = newPos + 1;
  links[link.prev].next = newPos + 1;

#ifdef KJ_DEBUG
  memset(&link, 0xff, sizeof(Link));
#endif
}

}  // namespace kj
