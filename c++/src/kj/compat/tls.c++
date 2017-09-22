// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#if KJ_HAS_OPENSSL

#include "tls.h"
#include "readiness-io.h"
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/conf.h>
#include <openssl/tls1.h>
#include <kj/debug.h>
#include <kj/vector.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define BIO_set_init(x,v)          (x->init=v)
#define BIO_get_data(x)            (x->ptr)
#define BIO_set_data(x,v)          (x->ptr=v)
#endif

namespace kj {
namespace {

// =======================================================================================
// misc helpers

KJ_NORETURN(void throwOpensslError());
void throwOpensslError() {
  // Call when an OpenSSL function returns an error code to convert that into an exception and
  // throw it.

  kj::Vector<kj::String> lines;
  while (unsigned long long error = ERR_get_error()) {
    char message[1024];
    ERR_error_string_n(error, message, sizeof(message));
    lines.add(kj::heapString(message));
  }
  kj::String message = kj::strArray(lines, "\n");
  KJ_FAIL_ASSERT("OpenSSL error", message);
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L && !defined(OPENSSL_IS_BORINGSSL)
// Older versions of OpenSSL don't define _up_ref() functions.

void EVP_PKEY_up_ref(EVP_PKEY* pkey) {
  CRYPTO_add(&pkey->references, 1, CRYPTO_LOCK_EVP_PKEY);
}

void X509_up_ref(X509* x509) {
  CRYPTO_add(&x509->references, 1, CRYPTO_LOCK_X509);
}

#endif

class OpenSslInit {
  // Initializes the OpenSSL library.
public:
  OpenSslInit() {
    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_config(nullptr);
  }
};

void ensureOpenSslInitialized() {
  // Initializes the OpenSSL library the first time it is called.
  static OpenSslInit init;
}

// =======================================================================================
// Implementation of kj::AsyncIoStream that applies TLS on top of some other AsyncIoStream.
//
// TODO(perf): OpenSSL's I/O abstraction layer, "BIO", is readiness-based, but AsyncIoStream is
//   completion-based. This forces us to use an intermediate buffer which wastes memory and incurs
//   redundant copies. We could improve the situation by creating a way to detect if the underlying
//   AsyncIoStream is simply wrapping a file descriptor (or other readiness-based stream?) and use
//   that directly if so.

class TlsConnection: public kj::AsyncIoStream {
public:
  TlsConnection(kj::Own<kj::AsyncIoStream> stream, SSL_CTX* ctx)
      : TlsConnection(*stream, ctx) {
    ownInner = kj::mv(stream);
  }

  TlsConnection(kj::AsyncIoStream& stream, SSL_CTX* ctx)
      : inner(stream), readBuffer(stream), writeBuffer(stream) {
    ssl = SSL_new(ctx);
    if (ssl == nullptr) {
      throwOpensslError();
    }

    BIO* bio = BIO_new(const_cast<BIO_METHOD*>(getBioVtable()));
    if (bio == nullptr) {
      SSL_free(ssl);
      throwOpensslError();
    }

    BIO_set_data(bio, this);
    BIO_set_init(bio, 1);
    SSL_set_bio(ssl, bio, bio);
  }

  kj::Promise<void> connect(kj::StringPtr expectedServerHostname) {
    if (!SSL_set_tlsext_host_name(ssl, expectedServerHostname.cStr())) {
      throwOpensslError();
    }

    X509_VERIFY_PARAM* verify = SSL_get0_param(ssl);
    if (verify == nullptr) {
      throwOpensslError();
    }

    if (X509_VERIFY_PARAM_set1_host(
        verify, expectedServerHostname.cStr(), expectedServerHostname.size()) <= 0) {
      throwOpensslError();
    }

    return sslCall([this]() { return SSL_connect(ssl); }).then([this](size_t) {
      X509* cert = SSL_get_peer_certificate(ssl);
      KJ_REQUIRE(cert != nullptr, "TLS peer provided no certificate");
      X509_free(cert);

      auto result = SSL_get_verify_result(ssl);
      if (result != X509_V_OK) {
        const char* reason = X509_verify_cert_error_string(result);
        KJ_FAIL_REQUIRE("TLS peer's certificate is not trusted", reason);
      }
    });
  }
  kj::Promise<void> accept() {
    // We are the server. Set SSL options to prefer server's cipher choice.
    SSL_set_options(ssl, SSL_OP_CIPHER_SERVER_PREFERENCE);

    return sslCall([this]() { return SSL_accept(ssl); }).ignoreResult();
  }

  ~TlsConnection() noexcept(false) {
    SSL_free(ssl);
  }

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

  Promise<void> write(const void* buffer, size_t size) override {
    return writeInternal(kj::arrayPtr(reinterpret_cast<const byte*>(buffer), size), nullptr);
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return writeInternal(pieces[0], pieces.slice(1, pieces.size()));
  }

  void shutdownWrite() override {
    KJ_REQUIRE(shutdownTask == nullptr, "already called shutdownWrite()");

    // TODO(soon): shutdownWrite() is problematic because it doesn't return a promise. It was
    //   designed to assume that it would only be called after all writes are finished and that
    //   there was no reason to block at that point, but SSL sessions don't fit this since they
    //   actually have to send a shutdown message.
    shutdownTask = sslCall([this]() {
      // The first SSL_shutdown() call is expected to return 0 and may flag a misleading error.
      int result = SSL_shutdown(ssl);
      return result == 0 ? 1 : result;
    }).ignoreResult().eagerlyEvaluate([](kj::Exception&& e) {
      KJ_LOG(ERROR, e);
    });
  }

  void abortRead() override {
    inner.abortRead();
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    inner.getsockopt(level, option, value, length);
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    inner.setsockopt(level, option, value, length);
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    inner.getsockname(addr, length);
  }
  void getpeername(struct sockaddr* addr, uint* length) override {
    inner.getpeername(addr, length);
  }

private:
  SSL* ssl;
  kj::AsyncIoStream& inner;
  kj::Own<kj::AsyncIoStream> ownInner;

  bool disconnected = false;
  kj::Maybe<kj::Promise<void>> shutdownTask;

  ReadyInputStreamWrapper readBuffer;
  ReadyOutputStreamWrapper writeBuffer;

  kj::Promise<size_t> tryReadInternal(
      void* buffer, size_t minBytes, size_t maxBytes, size_t alreadyDone) {
    if (disconnected) return alreadyDone;

    return sslCall([this,buffer,maxBytes]() { return SSL_read(ssl, buffer, maxBytes); })
        .then([this,buffer,minBytes,maxBytes,alreadyDone](size_t n) -> kj::Promise<size_t> {
      if (n >= minBytes) {
        return alreadyDone + n;
      } else {
        return tryReadInternal(reinterpret_cast<byte*>(buffer) + n,
                               minBytes - n, maxBytes - n, alreadyDone + n);
      }
    });
  }

  Promise<void> writeInternal(kj::ArrayPtr<const byte> first,
                              kj::ArrayPtr<const kj::ArrayPtr<const byte>> rest) {
    KJ_REQUIRE(shutdownTask == nullptr, "already called shutdownWrite()");

    return sslCall([this,first]() { return SSL_write(ssl, first.begin(), first.size()); })
        .then([this,first,rest](size_t n) -> kj::Promise<void> {
      if (n < first.size()) {
        return writeInternal(first.slice(n, first.size()), rest);
      } else if (rest.size() > 0) {
        return writeInternal(rest[0], rest.slice(1, rest.size()));
      } else {
        return kj::READY_NOW;
      }
    });
  }

  template <typename Func>
  kj::Promise<size_t> sslCall(Func&& func) {
    if (disconnected) return size_t(0);

    ssize_t result = func();

    if (result > 0) {
      return result;
    } else {
      int error = SSL_get_error(ssl, result);
      switch (error) {
        case SSL_ERROR_ZERO_RETURN:
          disconnected = true;
          return size_t(0);
        case SSL_ERROR_WANT_READ:
          return readBuffer.whenReady().then(kj::mvCapture(func,
              [this](Func&& func) mutable { return sslCall(kj::fwd<Func>(func)); }));
        case SSL_ERROR_WANT_WRITE:
          return writeBuffer.whenReady().then(kj::mvCapture(func,
              [this](Func&& func) mutable { return sslCall(kj::fwd<Func>(func)); }));
        case SSL_ERROR_SSL:
          throwOpensslError();
        case SSL_ERROR_SYSCALL:
          if (result == 0) {
            disconnected = true;
            return size_t(0);
          } else {
            // According to documentation we shouldn't get here, because our BIO never returns an
            // "error". But in practice we do get here sometimes when the peer disconnects
            // prematurely.
            KJ_FAIL_ASSERT("TLS protocol error");
          }
        default:
          KJ_FAIL_ASSERT("unexpected SSL error code", error);
      }
    }
  }

  static int bioRead(BIO* b, char* out, int outl) {
    BIO_clear_retry_flags(b);
    KJ_IF_MAYBE(n, reinterpret_cast<TlsConnection*>(BIO_get_data(b))->readBuffer
        .read(kj::arrayPtr(out, outl).asBytes())) {
      return *n;
    } else {
      BIO_set_retry_read(b);
      return -1;
    }
  }

  static int bioWrite(BIO* b, const char* in, int inl) {
    BIO_clear_retry_flags(b);
    KJ_IF_MAYBE(n, reinterpret_cast<TlsConnection*>(BIO_get_data(b))->writeBuffer
        .write(kj::arrayPtr(in, inl).asBytes())) {
      return *n;
    } else {
      BIO_set_retry_write(b);
      return -1;
    }
  }

  static long bioCtrl(BIO* b, int cmd, long num, void* ptr) {
    switch (cmd) {
      case BIO_CTRL_FLUSH:
        return 1;
      case BIO_CTRL_PUSH:
      case BIO_CTRL_POP:
        // Informational?
        return 0;
      default:
        KJ_LOG(WARNING, "unimplemented bio_ctrl", cmd);
        return 0;
    }
  }

  static int bioCreate(BIO* b) {
    BIO_set_data(b, nullptr);
    return 1;
  }

  static int bioDestroy(BIO* b) {
    // The BIO does NOT own the TlsConnection.
    return 1;
  }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  static const BIO_METHOD* getBioVtable() {
    static const BIO_METHOD VTABLE {
      BIO_TYPE_SOURCE_SINK,
      "KJ stream",
      TlsConnection::bioWrite,
      TlsConnection::bioRead,
      nullptr,  // puts
      nullptr,  // gets
      TlsConnection::bioCtrl,
      TlsConnection::bioCreate,
      TlsConnection::bioDestroy,
      nullptr
    };
    return &VTABLE;
  }
#else
  static const BIO_METHOD* getBioVtable() {
    static const BIO_METHOD* const vtable = makeBioVtable();
    return vtable;
  }
  static const BIO_METHOD* makeBioVtable() {
    BIO_METHOD* vtable = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "KJ stream");
    BIO_meth_set_write(vtable, TlsConnection::bioWrite);
    BIO_meth_set_read(vtable, TlsConnection::bioRead);
    BIO_meth_set_ctrl(vtable, TlsConnection::bioCtrl);
    BIO_meth_set_create(vtable, TlsConnection::bioCreate);
    BIO_meth_set_destroy(vtable, TlsConnection::bioDestroy);
    return vtable;
  }
#endif
};

// =======================================================================================
// Implementations of ConnectionReceiver, NetworkAddress, and Network as wrappers adding TLS.

class TlsConnectionReceiver: public kj::ConnectionReceiver {
public:
  TlsConnectionReceiver(TlsContext& tls, kj::Own<kj::ConnectionReceiver> inner)
      : tls(tls), inner(kj::mv(inner)) {}

  Promise<Own<AsyncIoStream>> accept() override {
    return inner->accept().then([this](kj::Own<AsyncIoStream> stream) {
      return tls.wrapServer(kj::mv(stream));
    });
  }

  uint getPort() override {
    return inner->getPort();
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    return inner->getsockopt(level, option, value, length);
  }

  void setsockopt(int level, int option, const void* value, uint length) override {
    return inner->setsockopt(level, option, value, length);
  }

private:
  TlsContext& tls;
  kj::Own<kj::ConnectionReceiver> inner;
};

class TlsNetworkAddress: public kj::NetworkAddress {
public:
  TlsNetworkAddress(TlsContext& tls, kj::String hostname, kj::Own<kj::NetworkAddress>&& inner)
      : tls(tls), hostname(kj::mv(hostname)), inner(kj::mv(inner)) {}

  Promise<Own<AsyncIoStream>> connect() override {
    // Note: It's unfortunately pretty common for people to assume they can drop the NetworkAddress
    //   as soon as connect() returns, and this works with the native network implementation.
    //   So, we make some copies here.
    auto& tlsRef = tls;
    auto hostnameCopy = kj::str(hostname);
    return inner->connect().then(kj::mvCapture(hostnameCopy,
        [&tlsRef](kj::String&& hostname, Own<AsyncIoStream>&& stream) {
      return tlsRef.wrapClient(kj::mv(stream), hostname);
    }));
  }

  Own<ConnectionReceiver> listen() override {
    return tls.wrapPort(inner->listen());
  }

  Own<NetworkAddress> clone() override {
    return kj::heap<TlsNetworkAddress>(tls, kj::str(hostname), inner->clone());
  }

  String toString() override {
    return kj::str("tls:", inner->toString());
  }

private:
  TlsContext& tls;
  kj::String hostname;
  kj::Own<kj::NetworkAddress> inner;
};

class TlsNetwork: public kj::Network {
public:
  TlsNetwork(TlsContext& tls, kj::Network& inner): tls(tls), inner(inner) {}
  TlsNetwork(TlsContext& tls, kj::Own<kj::Network> inner)
      : tls(tls), inner(*inner), ownInner(kj::mv(inner)) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint) override {
    kj::String hostname;
    KJ_IF_MAYBE(pos, addr.findFirst(':')) {
      hostname = kj::heapString(addr.slice(0, *pos));
    } else {
      hostname = kj::heapString(addr);
    }

    return inner.parseAddress(addr, portHint)
        .then(kj::mvCapture(hostname, [this](kj::String&& hostname, kj::Own<NetworkAddress>&& addr)
            -> kj::Own<kj::NetworkAddress> {
      return kj::heap<TlsNetworkAddress>(tls, kj::mv(hostname), kj::mv(addr));
    }));
  }

  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    KJ_UNIMPLEMENTED("TLS does not implement getSockaddr() because it needs to know hostnames");
  }

  Own<Network> restrictPeers(
      kj::ArrayPtr<const kj::StringPtr> allow,
      kj::ArrayPtr<const kj::StringPtr> deny = nullptr) override {
    // TODO(someday): Maybe we could implement the ability to specify CA or hostname restrictions?
    //   Or is it better to let people do that via the TlsContext? A neat thing about
    //   restrictPeers() is that it's easy to make user-configurable.
    return kj::heap<TlsNetwork>(tls, inner.restrictPeers(allow, deny));
  }

private:
  TlsContext& tls;
  kj::Network& inner;
  kj::Own<kj::Network> ownInner;
};

}  // namespace

// =======================================================================================
// class TlsContext

TlsContext::Options::Options()
    : useSystemTrustStore(true),
      verifyClients(false),
      minVersion(TlsVersion::TLS_1_0),
      cipherList("ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-DES-CBC3-SHA:ECDHE-RSA-DES-CBC3-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:DES-CBC3-SHA:!DSS") {}
// Cipher list is Mozilla's "intermediate" list, except with classic DH removed since we don't
// currently support setting dhparams. See:
//     https://mozilla.github.io/server-side-tls/ssl-config-generator/
//
// Classic DH is arguably obsolete and will only become more so as time passes, so perhaps we'll
// never bother.

TlsContext::TlsContext(Options options) {
  ensureOpenSslInitialized();

#if OPENSSL_VERSION_NUMBER >= 0x10100000L || defined(OPENSSL_IS_BORINGSSL)
  SSL_CTX* ctx = SSL_CTX_new(TLS_method());
#else
  SSL_CTX* ctx = SSL_CTX_new(SSLv23_method());
#endif

  if (ctx == nullptr) {
    throwOpensslError();
  }
  KJ_ON_SCOPE_FAILURE(SSL_CTX_free(ctx));

  // honor options.useSystemTrustStore
  if (options.useSystemTrustStore) {
    if (!SSL_CTX_set_default_verify_paths(ctx)) {
      throwOpensslError();
    }
  }

  // honor options.trustedCertificates
  if (options.trustedCertificates.size() > 0) {
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    if (store == nullptr) {
      throwOpensslError();
    }
    for (auto& cert: options.trustedCertificates) {
      if (!X509_STORE_add_cert(store, reinterpret_cast<X509*>(cert.chain[0]))) {
        throwOpensslError();
      }
    }
  }

  if (options.verifyClients) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
  }

  // honor options.minVersion
  long optionFlags = 0;
  if (options.minVersion > TlsVersion::SSL_3) {
    optionFlags |= SSL_OP_NO_SSLv3;
  }
  if (options.minVersion > TlsVersion::TLS_1_0) {
    optionFlags |= SSL_OP_NO_TLSv1;
  }
  if (options.minVersion > TlsVersion::TLS_1_1) {
    optionFlags |= SSL_OP_NO_TLSv1_1;
  }
  if (options.minVersion > TlsVersion::TLS_1_2) {
    optionFlags |= SSL_OP_NO_TLSv1_2;
  }
  SSL_CTX_set_options(ctx, optionFlags);  // note: never fails; returns new options bitmask

  // honor options.cipherList
  if (!SSL_CTX_set_cipher_list(ctx, options.cipherList.cStr())) {
    throwOpensslError();
  }

  // honor options.defaultKeypair
  KJ_IF_MAYBE(kp, options.defaultKeypair) {
    if (!SSL_CTX_use_PrivateKey(ctx, reinterpret_cast<EVP_PKEY*>(kp->privateKey.pkey))) {
      throwOpensslError();
    }

    if (!SSL_CTX_use_certificate(ctx, reinterpret_cast<X509*>(kp->certificate.chain[0]))) {
      throwOpensslError();
    }

    for (size_t i = 1; i < kj::size(kp->certificate.chain); i++) {
      X509* x509 = reinterpret_cast<X509*>(kp->certificate.chain[i]);
      if (x509 == nullptr) break;  // end of chain

      if (!SSL_CTX_add_extra_chain_cert(ctx, x509)) {
        throwOpensslError();
      }

      // SSL_CTX_add_extra_chain_cert() does NOT up the refcount itself.
      X509_up_ref(x509);
    }
  }

  // honor options.sniCallback
  KJ_IF_MAYBE(sni, options.sniCallback) {
    SSL_CTX_set_tlsext_servername_callback(ctx, &sniCallback);
    SSL_CTX_set_tlsext_servername_arg(ctx, sni);
  }

  this->ctx = ctx;
}

int TlsContext::sniCallback(void* sslp, int* ad, void* arg) {
  // The first parameter is actually type SSL*, but we didn't want to include the OpenSSL headers
  // from our header.
  //
  // The third parameter is actually type TlsSniCallback*.

  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    SSL* ssl = reinterpret_cast<SSL*>(sslp);
    TlsSniCallback& sni = *reinterpret_cast<TlsSniCallback*>(arg);

    const char* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (name != nullptr) {
      KJ_IF_MAYBE(kp, sni.getKey(name)) {
        if (!SSL_use_PrivateKey(ssl, reinterpret_cast<EVP_PKEY*>(kp->privateKey.pkey))) {
          throwOpensslError();
        }

        if (!SSL_use_certificate(ssl, reinterpret_cast<X509*>(kp->certificate.chain[0]))) {
          throwOpensslError();
        }

        if (!SSL_clear_chain_certs(ssl)) {
          throwOpensslError();
        }

        for (size_t i = 1; i < kj::size(kp->certificate.chain); i++) {
          X509* x509 = reinterpret_cast<X509*>(kp->certificate.chain[i]);
          if (x509 == nullptr) break;  // end of chain

          if (!SSL_add0_chain_cert(ssl, x509)) {
            throwOpensslError();
          }

          // SSL_add0_chain_cert() does NOT up the refcount itself.
          X509_up_ref(x509);
        }
      }
    }
  })) {
    KJ_LOG(ERROR, "exception when invoking SNI callback", *exception);
    *ad = SSL_AD_INTERNAL_ERROR;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  return SSL_TLSEXT_ERR_OK;
}

TlsContext::~TlsContext() noexcept(false) {
  SSL_CTX_free(reinterpret_cast<SSL_CTX*>(ctx));
}

kj::Promise<kj::Own<kj::AsyncIoStream>> TlsContext::wrapClient(
    kj::Own<kj::AsyncIoStream> stream, kj::StringPtr expectedServerHostname) {
  auto conn = kj::heap<TlsConnection>(kj::mv(stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->connect(expectedServerHostname);
  return promise.then(kj::mvCapture(conn, [](kj::Own<TlsConnection> conn)
      -> kj::Own<kj::AsyncIoStream> {
    return kj::mv(conn);
  }));
}

kj::Promise<kj::Own<kj::AsyncIoStream>> TlsContext::wrapServer(kj::Own<kj::AsyncIoStream> stream) {
  auto conn = kj::heap<TlsConnection>(kj::mv(stream), reinterpret_cast<SSL_CTX*>(ctx));
  auto promise = conn->accept();
  return promise.then(kj::mvCapture(conn, [](kj::Own<TlsConnection> conn)
      -> kj::Own<kj::AsyncIoStream> {
    return kj::mv(conn);
  }));
}

kj::Own<kj::ConnectionReceiver> TlsContext::wrapPort(kj::Own<kj::ConnectionReceiver> port) {
  return kj::heap<TlsConnectionReceiver>(*this, kj::mv(port));
}

kj::Own<kj::Network> TlsContext::wrapNetwork(kj::Network& network) {
  return kj::heap<TlsNetwork>(*this, network);
}

// =======================================================================================
// class TlsPrivateKey

TlsPrivateKey::TlsPrivateKey(kj::ArrayPtr<const byte> asn1) {
  ensureOpenSslInitialized();

  const byte* ptr = asn1.begin();
  pkey = d2i_AutoPrivateKey(nullptr, &ptr, asn1.size());
  if (pkey == nullptr) {
    throwOpensslError();
  }
}

TlsPrivateKey::TlsPrivateKey(kj::StringPtr pem) {
  ensureOpenSslInitialized();

  // const_cast apparently needed for older versions of OpenSSL.
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(pem.begin()), pem.size());
  KJ_DEFER(BIO_free(bio));

  pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  if (pkey == nullptr) {
    throwOpensslError();
  }
}

TlsPrivateKey::TlsPrivateKey(const TlsPrivateKey& other)
    : pkey(other.pkey) {
  if (pkey != nullptr) EVP_PKEY_up_ref(reinterpret_cast<EVP_PKEY*>(pkey));
}

TlsPrivateKey& TlsPrivateKey::operator=(const TlsPrivateKey& other) {
  if (pkey != other.pkey) {
    EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(pkey));
    pkey = other.pkey;
    if (pkey != nullptr) EVP_PKEY_up_ref(reinterpret_cast<EVP_PKEY*>(pkey));
  }
  return *this;
}

TlsPrivateKey::~TlsPrivateKey() noexcept(false) {
  EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(pkey));
}

// =======================================================================================
// class TlsCertificate

TlsCertificate::TlsCertificate(kj::ArrayPtr<const kj::ArrayPtr<const byte>> asn1) {
  ensureOpenSslInitialized();

  KJ_REQUIRE(asn1.size() > 0, "must provide at least one certificate in chain");
  KJ_REQUIRE(asn1.size() <= kj::size(chain),
      "exceeded maximum certificate chain length of 10");

  memset(chain, 0, sizeof(chain));

  for (auto i: kj::indices(asn1)) {
    auto p = asn1[i].begin();

    // "_AUX" apparently refers to some auxilliary information that can be appended to the
    // certificate, but should only be trusted for your own certificate, not the whole chain??
    // I don't really know, I'm just cargo-culting.
    chain[i] = i == 0 ? d2i_X509_AUX(nullptr, &p, asn1[i].size())
                      : d2i_X509(nullptr, &p, asn1[i].size());

    if (chain[i] == nullptr) {
      for (size_t j = 0; j < i; j++) {
        X509_free(reinterpret_cast<X509*>(chain[j]));
      }
      throwOpensslError();
    }
  }
}

TlsCertificate::TlsCertificate(kj::ArrayPtr<const byte> asn1)
    : TlsCertificate(kj::arrayPtr(&asn1, 1)) {}

TlsCertificate::TlsCertificate(kj::StringPtr pem) {
  ensureOpenSslInitialized();

  memset(chain, 0, sizeof(chain));

  // const_cast apparently needed for older versions of OpenSSL.
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(pem.begin()), pem.size());
  KJ_DEFER(BIO_free(bio));

  for (auto i: kj::indices(chain)) {
    // "_AUX" apparently refers to some auxilliary information that can be appended to the
    // certificate, but should only be trusted for your own certificate, not the whole chain??
    // I don't really know, I'm just cargo-culting.
    chain[i] = i == 0 ? PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr)
                      : PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (chain[i] == nullptr) {
      auto error = ERR_peek_last_error();
      if (i > 0 && ERR_GET_LIB(error) == ERR_LIB_PEM &&
          ERR_GET_REASON(error) == PEM_R_NO_START_LINE) {
        // EOF; we're done.
        ERR_clear_error();
        return;
      } else {
        for (size_t j = 0; j < i; j++) {
          X509_free(reinterpret_cast<X509*>(chain[j]));
        }
        throwOpensslError();
      }
    }
  }

  // We reached the chain length limit. Try to read one more to verify that the chain ends here.
  X509* dummy = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  if (dummy != nullptr) {
    X509_free(dummy);
    for (auto i: kj::indices(chain)) {
      X509_free(reinterpret_cast<X509*>(chain[i]));
    }
    KJ_FAIL_REQUIRE("exceeded maximum certificate chain length of 10");
  }
}

TlsCertificate::TlsCertificate(const TlsCertificate& other) {
  memcpy(chain, other.chain, sizeof(chain));
  for (void* p: chain) {
    if (p == nullptr) break;  // end of chain; quit early
    X509_up_ref(reinterpret_cast<X509*>(p));
  }
}

TlsCertificate& TlsCertificate::operator=(const TlsCertificate& other) {
  for (auto i: kj::indices(chain)) {
    if (chain[i] != other.chain[i]) {
      EVP_PKEY_free(reinterpret_cast<EVP_PKEY*>(chain[i]));
      chain[i] = other.chain[i];
      if (chain[i] != nullptr) X509_up_ref(reinterpret_cast<X509*>(chain[i]));
    } else if (chain[i] == nullptr) {
      // end of both chains; quit early
      break;
    }
  }
  return *this;
}

TlsCertificate::~TlsCertificate() noexcept(false) {
  for (void* p: chain) {
    if (p == nullptr) break;  // end of chain; quit early
    X509_free(reinterpret_cast<X509*>(p));
  }
}

}  // namespace kj

#endif  // KJ_HAS_OPENSSL
