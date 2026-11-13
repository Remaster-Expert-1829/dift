#pragma once

#include <memory>

// Forward-declare OpenSSL opaque types so callers do not need OpenSSL headers.
struct evp_pkey_st;
struct x509_st;
struct ssl_ctx_st;
struct ssl_st;

namespace spoink::support {

struct EvpPkeyDeleter {
  void operator()(evp_pkey_st* p) const noexcept;
};

struct X509Deleter {
  void operator()(x509_st* p) const noexcept;
};

struct SslCtxDeleter {
  void operator()(ssl_ctx_st* p) const noexcept;
};

struct SslDeleter {
  void operator()(ssl_st* p) const noexcept;
};

using EvpPkeyPtr = std::unique_ptr<evp_pkey_st, EvpPkeyDeleter>;
using X509Ptr = std::unique_ptr<x509_st, X509Deleter>;
using SslCtxPtr = std::unique_ptr<ssl_ctx_st, SslCtxDeleter>;
using SslPtr = std::unique_ptr<ssl_st, SslDeleter>;

}  // namespace spoink::support
