// This file was modified by Kenton Varda from code placed in the public domain.
// The code, which was originally C, was modified to give it a C++ interface.
// The original code bore the following notice:

/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * See md5.c for more information.
 */

// TODO(someday):  Put in KJ?

#ifndef CAPNP_COMPILER_MD5_H
#define CAPNP_COMPILER_MD5_H

#include <kj/string.h>
#include <kj/array.h>

namespace capnp {
namespace compiler {

class Md5 {
public:
  Md5();

  void update(kj::ArrayPtr<const kj::byte> data);
  inline void update(kj::ArrayPtr<const char> data) {
    return update(data.asBytes());
  }
  inline void update(kj::StringPtr data) {
    return update(data.asArray());
  }
  inline void update(const char* data) {
    return update(kj::StringPtr(data));
  }

  kj::ArrayPtr<const kj::byte> finish();
  kj::StringPtr finishAsHex();

private:
  /* Any 32-bit or wider unsigned integer data type will do */
  typedef unsigned int MD5_u32plus;

  bool finished = false;

  typedef struct {
    MD5_u32plus lo, hi;
    MD5_u32plus a, b, c, d;
    kj::byte buffer[64];
    MD5_u32plus block[16];
  } MD5_CTX;

  MD5_CTX ctx;

  const kj::byte* body(const kj::byte* ptr, size_t size);
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_MD5_H
