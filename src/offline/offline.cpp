#include "spoink/offline/offline.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <sqlite3.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utf8proc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <sstream>

#include "spoink/offline/initial_schema_sql.h"

namespace spoink::offline {
namespace {

support::Error make_invalid(const std::string& key) {
  return support::make_error(support::ErrorCode::kInvalidArgument, key);
}

support::Error make_resource(const std::string& key) {
  return support::make_error(support::ErrorCode::kResourceFailure, key);
}

bool valid_profile(const CdcProfile& profile) {
  return profile.polynomial != 0U && profile.window_bytes >= Limits::kMinWindowBytes &&
         profile.window_bytes <= Limits::kMaxWindowBytes && profile.min_bytes >= Limits::kMinChunkBytes &&
         profile.min_bytes <= profile.target_bytes && profile.target_bytes <= profile.max_bytes &&
         profile.max_bytes <= Limits::kMaxChunkBytes && profile.boundary_value <= profile.boundary_mask;
}

// ─── CBOR encoder helpers ────────────────────────────────────────────────────

void append_uint(std::vector<std::byte>* output, std::uint64_t value) {
  if (value < 24U) {
    output->push_back(static_cast<std::byte>(value));
    return;
  }
  if (value <= 0xffU) {
    output->push_back(std::byte{0x18U});
    output->push_back(static_cast<std::byte>(value));
    return;
  }
  if (value <= 0xffffU) {
    output->push_back(std::byte{0x19U});
  } else if (value <= 0xffffffffU) {
    output->push_back(std::byte{0x1aU});
  } else {
    output->push_back(std::byte{0x1bU});
  }
  std::size_t bytes = value <= 0xffffU ? 2U : (value <= 0xffffffffU ? 4U : 8U);
  for (std::size_t index = bytes; index > 0U; --index)
    output->push_back(static_cast<std::byte>(value >> ((index - 1U) * 8U)));
}

void append_text(std::vector<std::byte>* output, const std::string& text) {
  const auto length = static_cast<std::uint64_t>(text.size());
  const std::size_t start = output->size();
  append_uint(output, length);
  (*output)[start] |= std::byte{0x60U};
  for (const char value : text) output->push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
}

void append_bytes(std::vector<std::byte>* output, const std::array<std::byte, 32>& bytes) {
  output->push_back(std::byte{0x58U});
  output->push_back(std::byte{0x20U});
  output->insert(output->end(), bytes.begin(), bytes.end());
}

void append_map(std::vector<std::byte>* output, std::size_t count) {
  const std::size_t start = output->size();
  append_uint(output, count);
  (*output)[start] |= std::byte{0xa0U};
}

void append_array(std::vector<std::byte>* output, std::size_t count) {
  const std::size_t start = output->size();
  append_uint(output, count);
  (*output)[start] |= std::byte{0x80U};
}

void append_key_uint(std::vector<std::byte>* output, const std::string& key, std::uint64_t value) {
  append_text(output, key);
  append_uint(output, value);
}

// ─── Canonical CBOR structural validator ────────────────────────────────────

bool read_canonical_length(const std::vector<std::byte>& input, std::size_t* position, unsigned int additional,
                           std::uint64_t* value) {
  if (additional < 24U) {
    *value = additional;
    return true;
  }
  const std::size_t width = additional == 24U ? 1U : (additional == 25U ? 2U : (additional == 26U ? 4U : 8U));
  if (additional > 27U || input.size() - *position < width) return false;
  std::uint64_t parsed = 0U;
  for (std::size_t index = 0U; index < width; ++index)
    parsed = (parsed << 8U) | std::to_integer<unsigned char>(input[*position + index]);
  *position += width;
  if ((additional == 24U && parsed < 24U) || (additional == 25U && parsed <= 0xffU) ||
      (additional == 26U && parsed <= 0xffffU) || (additional == 27U && parsed <= 0xffffffffU))
    return false;
  *value = parsed;
  return true;
}

bool parse_canonical_item(const std::vector<std::byte>& input, std::size_t* position, std::size_t depth) {
  if (depth > 64U || *position >= input.size()) return false;
  const unsigned char initial = std::to_integer<unsigned char>(input[(*position)++]);
  const unsigned int major = initial >> 5U;
  const unsigned int additional = initial & 0x1fU;
  std::uint64_t length = 0U;
  if (major > 5U || !read_canonical_length(input, position, additional, &length)) return false;
  if (major < 2U) return true;
  if (length > input.size() - *position) return false;
  if (major == 2U) {
    *position += static_cast<std::size_t>(length);
    return true;
  }
  if (major == 3U) {
    const std::string text(reinterpret_cast<const char*>(input.data() + *position), static_cast<std::size_t>(length));
    utf8proc_uint8_t* normalized = utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t*>(text.c_str()));
    if (normalized == nullptr) return false;
    std::unique_ptr<utf8proc_uint8_t, decltype(&free)> holder(normalized, &free);
    const bool valid = std::string(reinterpret_cast<const char*>(normalized)) == text;
    *position += static_cast<std::size_t>(length);
    return valid;
  }
  if (major == 4U) {
    for (std::uint64_t index = 0U; index < length; ++index)
      if (!parse_canonical_item(input, position, depth + 1U)) return false;
    return true;
  }
  // major == 5: map
  std::vector<std::byte> previous_key;
  for (std::uint64_t index = 0U; index < length; ++index) {
    const std::size_t key_start = *position;
    if (!parse_canonical_item(input, position, depth + 1U)) return false;
    const std::vector<std::byte> key(input.begin() + static_cast<std::ptrdiff_t>(key_start),
                                     input.begin() + static_cast<std::ptrdiff_t>(*position));
    if (key.empty() || (std::to_integer<unsigned char>(key.front()) >> 5U) != 3U ||
        (!previous_key.empty() && !(previous_key < key)))
      return false;
    previous_key = key;
    if (!parse_canonical_item(input, position, depth + 1U)) return false;
  }
  return true;
}

// ─── Strict schema-1 CBOR decoder ───────────────────────────────────────────

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool decode_uint(const std::vector<std::byte>& input, std::size_t* pos, std::uint64_t* out) {
  if (*pos >= input.size()) return false;
  const unsigned char byte0 = std::to_integer<unsigned char>(input[(*pos)++]);
  const unsigned int major = byte0 >> 5U;
  if (major != 0U) return false;
  const unsigned int additional = byte0 & 0x1fU;
  std::uint64_t val = 0U;
  if (!read_canonical_length(input, pos, additional, &val)) return false;
  *out = val;
  return true;
}

bool decode_text(const std::vector<std::byte>& input, std::size_t* pos, std::string* out) {
  if (*pos >= input.size()) return false;
  const unsigned char byte0 = std::to_integer<unsigned char>(input[(*pos)++]);
  if ((byte0 >> 5U) != 3U) return false;
  const unsigned int additional = byte0 & 0x1fU;
  std::uint64_t len = 0U;
  if (!read_canonical_length(input, pos, additional, &len) || len > input.size() - *pos) return false;
  out->assign(reinterpret_cast<const char*>(input.data() + *pos), static_cast<std::size_t>(len));
  *pos += static_cast<std::size_t>(len);
  return true;
}

bool decode_bytes32(const std::vector<std::byte>& input, std::size_t* pos, std::array<std::byte, 32>* out) {
  if (*pos + 2U > input.size()) return false;
  // Expected: 0x58 0x20 followed by 32 bytes (canonical bstr of length 32)
  if (std::to_integer<unsigned char>(input[*pos]) != 0x58U) return false;
  if (std::to_integer<unsigned char>(input[*pos + 1U]) != 0x20U) return false;
  *pos += 2U;
  if (*pos + 32U > input.size()) return false;
  std::copy(input.begin() + static_cast<std::ptrdiff_t>(*pos), input.begin() + static_cast<std::ptrdiff_t>(*pos + 32U),
            out->begin());
  *pos += 32U;
  return true;
}

bool decode_map_header(const std::vector<std::byte>& input, std::size_t* pos, std::uint64_t* count) {
  if (*pos >= input.size()) return false;
  const unsigned char byte0 = std::to_integer<unsigned char>(input[(*pos)++]);
  if ((byte0 >> 5U) != 5U) return false;
  const unsigned int additional = byte0 & 0x1fU;
  return read_canonical_length(input, pos, additional, count);
}

bool decode_array_header(const std::vector<std::byte>& input, std::size_t* pos, std::uint64_t* count) {
  if (*pos >= input.size()) return false;
  const unsigned char byte0 = std::to_integer<unsigned char>(input[(*pos)++]);
  if ((byte0 >> 5U) != 4U) return false;
  const unsigned int additional = byte0 & 0x1fU;
  return read_canonical_length(input, pos, additional, count);
}

// Parse exactly the profile map {boundary_mask, boundary_value, max_bytes, min_bytes,
//                                polynomial, target_bytes, window_bytes}
// Keys must appear in RFC 8949 deterministic order (by encoded key length, then lex).
// Expected order (all len 9-14): max_bytes(9), min_bytes(9), polynomial(10), target_bytes(12),
//                                window_bytes(12), boundary_mask(13), boundary_value(14)
bool decode_profile_map(const std::vector<std::byte>& input, std::size_t* pos, CdcProfile* out) {
  std::uint64_t count = 0U;
  if (!decode_map_header(input, pos, &count) || count != 7U) return false;
  const char* expected_keys[] = {"max_bytes",    "min_bytes",     "polynomial",    "target_bytes",
                                 "window_bytes", "boundary_mask", "boundary_value"};
  std::uint64_t vals[7] = {};
  for (std::size_t i = 0U; i < 7U; ++i) {
    std::string key;
    if (!decode_text(input, pos, &key) || key != expected_keys[i]) return false;
    if (!decode_uint(input, pos, &vals[i])) return false;
  }
  out->max_bytes = static_cast<std::size_t>(vals[0]);
  out->min_bytes = static_cast<std::size_t>(vals[1]);
  out->polynomial = vals[2];
  out->target_bytes = static_cast<std::size_t>(vals[3]);
  out->window_bytes = static_cast<std::size_t>(vals[4]);
  out->boundary_mask = vals[5];
  out->boundary_value = vals[6];
  return valid_profile(*out);
}

// Parse a chunk map: {length, offset, sha256} (RFC 8949 order: length(6), offset(6), sha256(6))
bool decode_chunk_entry(const std::vector<std::byte>& input, std::size_t* pos, std::uint64_t* length_out,
                        std::uint64_t* offset_out, std::array<std::byte, 32>* sha256_out) {
  std::uint64_t count = 0U;
  if (!decode_map_header(input, pos, &count) || count != 3U) return false;
  std::string k1, k2, k3;
  if (!decode_text(input, pos, &k1) || k1 != "length") return false;
  if (!decode_uint(input, pos, length_out)) return false;
  if (!decode_text(input, pos, &k2) || k2 != "offset") return false;
  if (!decode_uint(input, pos, offset_out)) return false;
  if (!decode_text(input, pos, &k3) || k3 != "sha256") return false;
  if (!decode_bytes32(input, pos, sha256_out)) return false;
  return true;
}

// ─── Chunk store path ────────────────────────────────────────────────────────

std::filesystem::path chunk_path(const std::filesystem::path& root, const support::Sha256Digest& digest) {
  const std::string hex = digest.hex();
  return root / "chunks" / hex.substr(0U, 2U) / hex.substr(2U, 2U) / hex;
}

// ─── Random hex suffix for unpredictable temp names ─────────────────────────

std::string random_hex_suffix() {
  std::array<unsigned char, 8> buf{};
  // getrandom fills buf deterministically from the kernel entropy pool
  if (::getentropy(buf.data(), buf.size()) != 0) {
    // Fallback: use std::random_device
    std::random_device rd;
    std::uint64_t v = (static_cast<std::uint64_t>(rd()) << 32U) ^ rd();
    for (std::size_t i = 0U; i < 8U; ++i) buf[i] = static_cast<unsigned char>(v >> (i * 8U));
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(16U);
  for (unsigned char c : buf) {
    result += hex[c >> 4U];
    result += hex[c & 0x0fU];
  }
  return result;
}

// ─── fsync helpers ───────────────────────────────────────────────────────────

// Flush file data and metadata to storage.
bool fsync_fd(int fd) { return ::fsync(fd) == 0; }

// Open a directory and fsync it (ensures directory entries are durable).
bool fsync_dir(const std::filesystem::path& dir) {
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) return false;
  const bool ok = (::fsync(fd) == 0);
  ::close(fd);
  return ok;
}

}  // namespace

// ─── StateRootLock ───────────────────────────────────────────────────────────

StateRootLock::~StateRootLock() {
  if (fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
  }
}
StateRootLock::StateRootLock(StateRootLock&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
StateRootLock& StateRootLock::operator=(StateRootLock&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::flock(fd_, LOCK_UN);
      ::close(fd_);
    }
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

support::Result<StateRootLock> StateRootLock::acquire(const std::filesystem::path& state_root) {
  std::error_code ec;
  std::filesystem::create_directories(state_root, ec);
  if (ec) return support::Result<StateRootLock>::failure(make_resource("m1.lock.mkdir"));
  const auto lock_path = state_root / ".spoink-instance.lock";
  const int fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0) return support::Result<StateRootLock>::failure(make_resource("m1.lock.open"));
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    return support::Result<StateRootLock>::failure(make_resource("m1.lock.held"));
  }
  return support::Result<StateRootLock>::success(StateRootLock(fd));
}

// ─── Path / selection ────────────────────────────────────────────────────────

support::Result<std::string> normalize_manifest_path(const std::string& path) {
  if (path.empty() || path.size() > Limits::kMaxPathBytes || path[0] == '/' || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos)
    return support::Result<std::string>::failure(make_invalid("m1.path.invalid"));
  utf8proc_uint8_t* normalized = nullptr;
  normalized = utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t*>(path.c_str()));
  if (normalized == nullptr) return support::Result<std::string>::failure(make_invalid("m1.path.utf8"));
  std::unique_ptr<utf8proc_uint8_t, decltype(&free)> holder(normalized, &free);
  std::string result(reinterpret_cast<const char*>(normalized));
  if (result != path) return support::Result<std::string>::failure(make_invalid("m1.path.not_nfc"));
  if (std::any_of(result.begin(), result.end(), [](unsigned char value) { return std::iscntrl(value) != 0; }))
    return support::Result<std::string>::failure(make_invalid("m1.path.control"));
  std::size_t begin = 0U;
  while (begin <= result.size()) {
    const auto end = result.find('/', begin);
    const auto segment = result.substr(begin, end - begin);
    if (segment.empty() || segment == "." || segment == ".." || segment.size() > Limits::kMaxSegmentBytes)
      return support::Result<std::string>::failure(make_invalid("m1.path.segment"));
    if (end == std::string::npos) break;
    begin = end + 1U;
  }
  return support::Result<std::string>::success(std::move(result));
}

support::Result<std::vector<SelectedFile>> select_files(const std::vector<std::filesystem::path>& inputs,
                                                        const std::filesystem::path& root) {
  std::vector<SelectedFile> files;
  std::set<std::string> seen;
  std::error_code error;
  const auto base = std::filesystem::weakly_canonical(root, error);
  if (error) return support::Result<std::vector<SelectedFile>>::failure(make_resource("m1.select.root"));
  for (const auto& input : inputs) {
    const auto input_state = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(input_state))
      return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.symlink"));
    const auto resolved = std::filesystem::weakly_canonical(input, error);
    if (error) return support::Result<std::vector<SelectedFile>>::failure(make_resource("m1.select.input"));
    const auto relative_input = resolved.lexically_relative(base);
    if (relative_input.empty() || *relative_input.begin() == "..")
      return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.escape"));
    const auto state = std::filesystem::symlink_status(resolved, error);
    if (error || std::filesystem::is_symlink(state))
      return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.symlink"));
    const auto add = [&](const std::filesystem::path& source) -> bool {
      const auto resolved_source = std::filesystem::weakly_canonical(source, error);
      if (error) return false;
      const auto relative = resolved_source.lexically_relative(base);
      if (relative.empty() || *relative.begin() == "..") return false;
      const auto normalized = normalize_manifest_path(relative.generic_string());
      if (!normalized || !std::filesystem::is_regular_file(source, error) ||
          !seen.insert(*normalized.value_if()).second)
        return false;
      const auto size = std::filesystem::file_size(source, error);
      if (error) return false;
      files.push_back({source, *normalized.value_if(), size});
      return true;
    };
    if (std::filesystem::is_regular_file(state)) {
      if (!add(input)) return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.file"));
    } else if (std::filesystem::is_directory(state)) {
      for (std::filesystem::recursive_directory_iterator it(input, std::filesystem::directory_options::none, error),
           end;
           !error && it != end; it.increment(error)) {
        if (std::filesystem::is_symlink(it->symlink_status(error)) ||
            (!std::filesystem::is_directory(it->status(error)) && !std::filesystem::is_regular_file(it->status(error))))
          return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.entry"));
        if (std::filesystem::is_regular_file(it->status(error)) && !add(it->path()))
          return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.file"));
      }
      if (error) return support::Result<std::vector<SelectedFile>>::failure(make_resource("m1.select.read"));
    } else
      return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.type"));
  }
  if (files.size() > Limits::kMaxFiles)
    return support::Result<std::vector<SelectedFile>>::failure(make_invalid("m1.select.count"));
  std::sort(files.begin(), files.end(), [](const SelectedFile& left, const SelectedFile& right) {
    return left.manifest_path < right.manifest_path;
  });
  return support::Result<std::vector<SelectedFile>>::success(std::move(files));
}

// ─── SHA-256 ─────────────────────────────────────────────────────────────────

support::Result<support::Sha256Digest> sha256(const std::vector<std::byte>& bytes) {
  std::array<unsigned char, 32> digest{};
  unsigned int length = 0U;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 ||
      length != digest.size())
    return support::Result<support::Sha256Digest>::failure(make_resource("m1.sha256"));
  std::array<std::byte, 32> result{};
  for (std::size_t index = 0U; index < result.size(); ++index) result[index] = static_cast<std::byte>(digest[index]);
  return support::Result<support::Sha256Digest>::success(support::Sha256Digest(result));
}

// ─── Rabin sliding-window CDC ────────────────────────────────────────────────
//
// Maintains a ring buffer of window_bytes bytes. The rolling hash over the
// window is: hash[i] = hash[i-1] * polynomial + in_byte - out_byte * pow_w
// where pow_w = polynomial^window_bytes (regular 64-bit arithmetic overflow).
//
// Boundary: hash & mask == value AND length >= min_bytes.
// Cut at max_bytes regardless of hash.
//
// This hash depends only on the last window_bytes bytes (true sliding window),
// so the same pattern of bytes always produces the same boundaries regardless
// of position within the stream (subject to min/max limits).

support::Result<std::vector<Chunk>> chunk_bytes(const std::vector<std::byte>& bytes, const CdcProfile& profile) {
  if (!valid_profile(profile)) return support::Result<std::vector<Chunk>>::failure(make_invalid("m1.cdc.profile"));
  if (bytes.empty()) return support::Result<std::vector<Chunk>>::success({});

  // Precompute polynomial^window_bytes
  std::uint64_t pow_w = 1U;
  for (std::size_t i = 0U; i < profile.window_bytes; ++i) pow_w *= profile.polynomial;

  // Ring buffer initialized to zero (first window_bytes iterations slide over implicit zeros)
  std::vector<std::uint8_t> ring(profile.window_bytes, 0U);
  std::size_t ring_pos = 0U;
  std::uint64_t hash = 0U;

  std::vector<Chunk> chunks;
  std::size_t start = 0U;

  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    const std::uint8_t in_byte = std::to_integer<std::uint8_t>(bytes[index]);
    const std::uint8_t out_byte = ring[ring_pos];
    ring[ring_pos] = in_byte;
    ring_pos = (ring_pos + 1U) % profile.window_bytes;
    hash = hash * profile.polynomial + in_byte - static_cast<std::uint64_t>(out_byte) * pow_w;

    const std::size_t length = index - start + 1U;
    const bool cut = (length >= profile.max_bytes) ||
                     (length >= profile.min_bytes && (hash & profile.boundary_mask) == profile.boundary_value);
    if (cut) {
      std::vector<std::byte> value(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(index + 1U));
      const auto digest = sha256(value);
      if (!digest) return support::Result<std::vector<Chunk>>::failure(digest.error());
      chunks.push_back({static_cast<std::uint64_t>(start), std::move(value), *digest.value_if()});
      start = index + 1U;
      // NOTE: do NOT reset hash; the window keeps sliding across chunk boundaries
    }
  }
  if (start < bytes.size()) {
    std::vector<std::byte> value(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
    const auto digest = sha256(value);
    if (!digest) return support::Result<std::vector<Chunk>>::failure(digest.error());
    chunks.push_back({static_cast<std::uint64_t>(start), std::move(value), *digest.value_if()});
  }
  if (chunks.size() > Limits::kMaxChunkReferences)
    return support::Result<std::vector<Chunk>>::failure(make_invalid("m1.cdc.count"));
  return support::Result<std::vector<Chunk>>::success(std::move(chunks));
}

// ─── Manifest encoder ────────────────────────────────────────────────────────

support::Result<CanonicalManifest> build_manifest(std::vector<ManifestFile> files, const CdcProfile& profile,
                                                  std::uint64_t created_at_unix_ms) {
  if (!valid_profile(profile) || files.size() > Limits::kMaxFiles)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.manifest.input"));
  std::sort(files.begin(), files.end(),
            [](const ManifestFile& left, const ManifestFile& right) { return left.path < right.path; });

  std::vector<std::byte> encoded;
  // Top-level map: 5 keys in RFC 8949 deterministic order (ascending encoded-key length,
  // then lexicographic). "files"(5) < "schema"(6) < "profile"(7) < "profile_id"(10) < "created_at_unix_ms"(18)
  append_map(&encoded, 5U);

  append_text(&encoded, "files");
  append_array(&encoded, files.size());
  for (const auto& file : files) {
    const auto path = normalize_manifest_path(file.path);
    if (!path) return support::Result<CanonicalManifest>::failure(path.error());
    // File map: 4 keys. "path"(4) < "size"(4,lex) < "chunks"(6) < "sha256"(6,lex)
    append_map(&encoded, 4U);
    append_text(&encoded, "path");
    append_text(&encoded, *path.value_if());
    append_key_uint(&encoded, "size", file.size);
    append_text(&encoded, "chunks");
    append_array(&encoded, file.chunks.size());
    std::uint64_t expected_offset = 0U;
    for (const auto& chunk : file.chunks) {
      if (chunk.offset != expected_offset)
        return support::Result<CanonicalManifest>::failure(make_invalid("m1.manifest.offset"));
      expected_offset += chunk.bytes.size();
      // Chunk map: 3 keys. "length"(6) < "offset"(6,lex) < "sha256"(6,lex)
      append_map(&encoded, 3U);
      append_key_uint(&encoded, "length", chunk.bytes.size());
      append_key_uint(&encoded, "offset", chunk.offset);
      append_text(&encoded, "sha256");
      append_bytes(&encoded, chunk.digest.bytes());
    }
    if (expected_offset != file.size)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.manifest.size"));
    append_text(&encoded, "sha256");
    append_bytes(&encoded, file.digest.bytes());
  }

  append_key_uint(&encoded, "schema", 1U);

  append_text(&encoded, "profile");
  // Profile map: 7 keys in RFC 8949 order by length then lex.
  // max_bytes(9) < min_bytes(9,lex) < polynomial(10) < target_bytes(12) < window_bytes(12,lex)
  // < boundary_mask(13) < boundary_value(14)
  append_map(&encoded, 7U);
  append_key_uint(&encoded, "max_bytes", profile.max_bytes);
  append_key_uint(&encoded, "min_bytes", profile.min_bytes);
  append_key_uint(&encoded, "polynomial", profile.polynomial);
  append_key_uint(&encoded, "target_bytes", profile.target_bytes);
  append_key_uint(&encoded, "window_bytes", profile.window_bytes);
  append_key_uint(&encoded, "boundary_mask", profile.boundary_mask);
  append_key_uint(&encoded, "boundary_value", profile.boundary_value);

  append_text(&encoded, "profile_id");
  // Emit "rabin-v1" when the profile matches the frozen selection; otherwise
  // emit "experimental-parameterized" so tests with custom profiles stay valid.
  const CdcProfile rv1 = rabin_v1_profile();
  const bool is_rabin_v1 = profile.polynomial == rv1.polynomial && profile.window_bytes == rv1.window_bytes &&
                           profile.min_bytes == rv1.min_bytes && profile.target_bytes == rv1.target_bytes &&
                           profile.max_bytes == rv1.max_bytes && profile.boundary_mask == rv1.boundary_mask &&
                           profile.boundary_value == rv1.boundary_value;
  append_text(&encoded, is_rabin_v1 ? "rabin-v1" : "experimental-parameterized");

  append_key_uint(&encoded, "created_at_unix_ms", created_at_unix_ms);

  if (encoded.size() > Limits::kMaxManifestBytes)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.manifest.limit"));
  const auto digest = sha256(encoded);
  if (!digest) return support::Result<CanonicalManifest>::failure(digest.error());
  return support::Result<CanonicalManifest>::success({std::move(encoded), *digest.value_if(), std::move(files)});
}

support::Result<void> validate_canonical_manifest(const std::vector<std::byte>& bytes) {
  if (bytes.empty() || bytes.size() > Limits::kMaxManifestBytes)
    return support::Result<void>::failure(make_invalid("m1.manifest.limit"));
  std::size_t position = 0U;
  const bool valid = parse_canonical_item(bytes, &position, 0U) && position == bytes.size() &&
                     (std::to_integer<unsigned char>(bytes.front()) >> 5U) == 5U;
  return valid ? support::Result<void>::success()
               : support::Result<void>::failure(make_invalid("m1.manifest.noncanonical"));
}

// ─── Strict schema-1 decoder ─────────────────────────────────────────────────

support::Result<CanonicalManifest> decode_manifest(const std::vector<std::byte>& bytes) {
  if (bytes.empty() || bytes.size() > Limits::kMaxManifestBytes)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.limit"));

  // First verify structural canonicality
  {
    const auto structural = validate_canonical_manifest(bytes);
    if (!structural) return support::Result<CanonicalManifest>::failure(structural.error());
  }

  std::size_t pos = 0U;

  // Top-level must be a map with exactly 5 keys
  std::uint64_t top_count = 0U;
  if (!decode_map_header(bytes, &pos, &top_count) || top_count != 5U)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.top_map"));

  // Keys in required RFC 8949 order: files, schema, profile, profile_id, created_at_unix_ms
  // files
  std::string key1;
  if (!decode_text(bytes, &pos, &key1) || key1 != "files")
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.key_files"));
  std::uint64_t file_count = 0U;
  if (!decode_array_header(bytes, &pos, &file_count) || file_count > Limits::kMaxFiles)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.files_count"));

  std::vector<ManifestFile> decoded_files;
  decoded_files.reserve(static_cast<std::size_t>(file_count));
  std::string prev_path;
  for (std::uint64_t fi = 0U; fi < file_count; ++fi) {
    std::uint64_t file_map_count = 0U;
    if (!decode_map_header(bytes, &pos, &file_map_count) || file_map_count != 4U)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_map"));

    // File map keys: path(4), size(4,lex), chunks(6), sha256(6,lex)
    std::string k;
    if (!decode_text(bytes, &pos, &k) || k != "path")
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_path_key"));
    std::string path;
    if (!decode_text(bytes, &pos, &path))
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_path"));
    const auto normalized = normalize_manifest_path(path);
    if (!normalized) return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_path_invalid"));
    if (!prev_path.empty() && *normalized.value_if() <= prev_path)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_order"));
    prev_path = *normalized.value_if();

    if (!decode_text(bytes, &pos, &k) || k != "size")
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_size_key"));
    std::uint64_t file_size = 0U;
    if (!decode_uint(bytes, &pos, &file_size) || file_size > Limits::kMaxFileSizeBytes)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.file_size"));

    if (!decode_text(bytes, &pos, &k) || k != "chunks")
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.chunks_key"));
    std::uint64_t chunk_count = 0U;
    if (!decode_array_header(bytes, &pos, &chunk_count) || chunk_count > Limits::kMaxChunkReferences)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.chunk_count"));
    // Empty file must have zero chunks
    if (file_size == 0U && chunk_count != 0U)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.empty_chunks"));

    std::vector<Chunk> decoded_chunks;
    decoded_chunks.reserve(static_cast<std::size_t>(chunk_count));
    std::uint64_t running_offset = 0U;
    for (std::uint64_t ci = 0U; ci < chunk_count; ++ci) {
      std::uint64_t chunk_len = 0U, chunk_off = 0U;
      std::array<std::byte, 32> chunk_hash{};
      if (!decode_chunk_entry(bytes, &pos, &chunk_len, &chunk_off, &chunk_hash))
        return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.chunk"));
      if (chunk_off != running_offset || chunk_len == 0U)
        return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.chunk_offset"));
      running_offset += chunk_len;
      decoded_chunks.push_back({chunk_off, {}, support::Sha256Digest(chunk_hash)});
      decoded_chunks.back().bytes.resize(static_cast<std::size_t>(chunk_len));
    }
    if (running_offset != file_size)
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.chunk_total"));

    if (!decode_text(bytes, &pos, &k) || k != "sha256")
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.sha256_key"));
    std::array<std::byte, 32> file_hash{};
    if (!decode_bytes32(bytes, &pos, &file_hash))
      return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.sha256"));

    decoded_files.push_back(
        {*normalized.value_if(), file_size, support::Sha256Digest(file_hash), std::move(decoded_chunks)});
  }

  // schema
  std::string key2;
  if (!decode_text(bytes, &pos, &key2) || key2 != "schema")
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.key_schema"));
  std::uint64_t schema_ver = 0U;
  if (!decode_uint(bytes, &pos, &schema_ver) || schema_ver != 1U)
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.schema_version"));

  // profile
  std::string key3;
  if (!decode_text(bytes, &pos, &key3) || key3 != "profile")
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.key_profile"));
  CdcProfile decoded_profile{};
  if (!decode_profile_map(bytes, &pos, &decoded_profile))
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.profile"));

  // profile_id
  std::string key4;
  if (!decode_text(bytes, &pos, &key4) || key4 != "profile_id")
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.key_profile_id"));
  std::string profile_id;
  if (!decode_text(bytes, &pos, &profile_id) || profile_id.empty())
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.profile_id"));

  // created_at_unix_ms
  std::string key5;
  if (!decode_text(bytes, &pos, &key5) || key5 != "created_at_unix_ms")
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.key_ts"));
  std::uint64_t created_at = 0U;
  if (!decode_uint(bytes, &pos, &created_at))
    return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.ts"));

  if (pos != bytes.size()) return support::Result<CanonicalManifest>::failure(make_invalid("m1.decode.trailing"));

  const auto digest = sha256(bytes);
  if (!digest) return support::Result<CanonicalManifest>::failure(digest.error());

  return support::Result<CanonicalManifest>::success({bytes, *digest.value_if(), std::move(decoded_files)});
}

// ─── MetadataStore ───────────────────────────────────────────────────────────

MetadataStore::~MetadataStore() {
  if (database_ != nullptr) sqlite3_close(database_);
}
MetadataStore::MetadataStore(MetadataStore&& other) noexcept : database_(other.database_) { other.database_ = nullptr; }
MetadataStore& MetadataStore::operator=(MetadataStore&& other) noexcept {
  if (this != &other) {
    if (database_ != nullptr) sqlite3_close(database_);
    database_ = other.database_;
    other.database_ = nullptr;
  }
  return *this;
}

support::Result<MetadataStore> MetadataStore::open(const std::filesystem::path& state_root) {
  std::error_code error;
  std::filesystem::create_directories(state_root, error);
  if (error) return support::Result<MetadataStore>::failure(make_resource("m1.store.root"));
  sqlite3* database = nullptr;
  if (sqlite3_open_v2((state_root / "metadata.sqlite3").c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr) != SQLITE_OK) {
    if (database != nullptr) sqlite3_close(database);
    return support::Result<MetadataStore>::failure(make_resource("m1.store.open"));
  }

  // Enable WAL and foreign keys, then apply schema if needed
  const std::string setup_sql = "PRAGMA journal_mode=WAL;PRAGMA foreign_keys=ON;";
  char* message = nullptr;
  if (sqlite3_exec(database, setup_sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
    sqlite3_free(message);
    sqlite3_close(database);
    return support::Result<MetadataStore>::failure(make_resource("m1.store.pragma"));
  }

  // Check current schema version
  sqlite3_stmt* ver_stmt = nullptr;
  int existing_version = 0;
  if (sqlite3_prepare_v2(database, "SELECT MAX(version) FROM schema_migrations", -1, &ver_stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(ver_stmt) == SQLITE_ROW) existing_version = sqlite3_column_int(ver_stmt, 0);
    sqlite3_finalize(ver_stmt);
  }

  if (existing_version == 0) {
    // Fresh database: apply schema 1
    const std::string migration_sql = "BEGIN;" + std::string(kInitialSchemaSql) + "COMMIT;";
    if (sqlite3_exec(database, migration_sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
      sqlite3_free(message);
      sqlite3_close(database);
      return support::Result<MetadataStore>::failure(make_resource("m1.store.migrate"));
    }
  } else if (existing_version > 1) {
    // Newer schema: refuse to open
    sqlite3_close(database);
    return support::Result<MetadataStore>::failure(make_invalid("m1.store.schema_newer"));
  }
  // existing_version == 1: already at the correct version

  return support::Result<MetadataStore>::success(MetadataStore(database));
}

support::Result<int> MetadataStore::schema_version() const {
  if (database_ == nullptr) return support::Result<int>::failure(make_invalid("m1.store.closed"));
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(database_, "SELECT MAX(version) FROM schema_migrations", -1, &stmt, nullptr) != SQLITE_OK)
    return support::Result<int>::failure(make_resource("m1.store.version"));
  const int rc = sqlite3_step(stmt);
  const int ver = (rc == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
  sqlite3_finalize(stmt);
  return (rc == SQLITE_ROW) ? support::Result<int>::success(ver)
                            : support::Result<int>::failure(make_resource("m1.store.version"));
}

support::Result<void> MetadataStore::verify_wal_and_fk() const {
  if (database_ == nullptr) return support::Result<void>::failure(make_invalid("m1.store.closed"));
  // Verify WAL mode
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(database_, "PRAGMA journal_mode", -1, &stmt, nullptr) != SQLITE_OK)
    return support::Result<void>::failure(make_resource("m1.store.wal_check"));
  bool wal_ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    wal_ok = (mode != nullptr && std::string(mode) == "wal");
  }
  sqlite3_finalize(stmt);
  if (!wal_ok) return support::Result<void>::failure(make_resource("m1.store.wal_mode"));
  // Verify foreign keys
  if (sqlite3_prepare_v2(database_, "PRAGMA foreign_keys", -1, &stmt, nullptr) != SQLITE_OK)
    return support::Result<void>::failure(make_resource("m1.store.fk_check"));
  bool fk_ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) fk_ok = (sqlite3_column_int(stmt, 0) == 1);
  sqlite3_finalize(stmt);
  return fk_ok ? support::Result<void>::success()
               : support::Result<void>::failure(make_resource("m1.store.fk_disabled"));
}

support::Result<void> MetadataStore::record_verified(const support::Sha256Digest& digest, std::uint64_t byte_length) {
  if (database_ == nullptr || byte_length > static_cast<std::uint64_t>(INT64_MAX))
    return support::Result<void>::failure(make_invalid("m1.store.length"));
  if (byte_length == 0U) return support::Result<void>::failure(make_invalid("m1.store.empty_chunk"));
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "INSERT INTO chunk_catalog(chunk_hash,plaintext_length,state,store_relpath,created_at_ms,verified_at_ms) "
      "VALUES(?1,?2,'VERIFIED',?3,?4,?4) ON CONFLICT(chunk_hash) DO UPDATE SET state='VERIFIED' "
      "WHERE plaintext_length=excluded.plaintext_length;";
  if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK)
    return support::Result<void>::failure(make_resource("m1.store.prepare"));
  const auto now_ms = static_cast<sqlite3_int64>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
  sqlite3_bind_blob(statement, 1, digest.bytes().data(), static_cast<int>(digest.bytes().size()), SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(byte_length));
  const std::string relpath =
      "chunks/" + digest.hex().substr(0U, 2U) + "/" + digest.hex().substr(2U, 2U) + "/" + digest.hex();
  sqlite3_bind_text(statement, 3, relpath.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 4, now_ms);
  const int result = sqlite3_step(statement);
  const int changes = sqlite3_changes(database_);
  sqlite3_finalize(statement);
  if (result != SQLITE_DONE) return support::Result<void>::failure(make_resource("m1.store.write"));
  return changes == 0 ? support::Result<void>::failure(make_invalid("m1.store.hash_metadata_conflict"))
                      : support::Result<void>::success();
}

support::Result<bool> MetadataStore::is_verified(const support::Sha256Digest& digest) const {
  if (database_ == nullptr) return support::Result<bool>::failure(make_invalid("m1.store.closed"));
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(database_, "SELECT 1 FROM chunk_catalog WHERE chunk_hash=?1 AND state='VERIFIED'", -1, &stmt,
                         nullptr) != SQLITE_OK)
    return support::Result<bool>::failure(make_resource("m1.store.query"));
  sqlite3_bind_blob(stmt, 1, digest.bytes().data(), static_cast<int>(digest.bytes().size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) return support::Result<bool>::success(true);
  if (rc == SQLITE_DONE) return support::Result<bool>::success(false);
  return support::Result<bool>::failure(make_resource("m1.store.query"));
}

support::Result<std::uint64_t> MetadataStore::verified_count() const {
  sqlite3_stmt* statement = nullptr;
  if (database_ == nullptr || sqlite3_prepare_v2(database_, "SELECT COUNT(*) FROM chunk_catalog WHERE state='VERIFIED'",
                                                 -1, &statement, nullptr) != SQLITE_OK)
    return support::Result<std::uint64_t>::failure(make_resource("m1.store.count"));
  const int result = sqlite3_step(statement);
  const auto count = result == SQLITE_ROW ? static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)) : 0U;
  sqlite3_finalize(statement);
  return result == SQLITE_ROW ? support::Result<std::uint64_t>::success(count)
                              : support::Result<std::uint64_t>::failure(make_resource("m1.store.count"));
}

support::Result<void> MetadataStore::mark_quarantined(const support::Sha256Digest& digest, const std::string& reason) {
  if (database_ == nullptr) return support::Result<void>::failure(make_invalid("m1.store.closed"));
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(database_,
                         "UPDATE chunk_catalog SET state='QUARANTINED',quarantine_reason=?2 WHERE chunk_hash=?1", -1,
                         &stmt, nullptr) != SQLITE_OK)
    return support::Result<void>::failure(make_resource("m1.store.quarantine"));
  sqlite3_bind_blob(stmt, 1, digest.bytes().data(), static_cast<int>(digest.bytes().size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE ? support::Result<void>::success()
                           : support::Result<void>::failure(make_resource("m1.store.quarantine"));
}

support::Result<void> MetadataStore::recompute_ref_counts() {
  if (database_ == nullptr) return support::Result<void>::failure(make_invalid("m1.store.closed"));
  const char* sql =
      "BEGIN;"
      "UPDATE chunk_catalog SET manifest_ref_count = ("
      "  SELECT COUNT(*) FROM file_chunk_refs fcr"
      "  JOIN transfer_files tf ON fcr.transfer_id=tf.transfer_id AND fcr.file_id=tf.file_id"
      "  WHERE fcr.chunk_hash=chunk_catalog.chunk_hash AND tf.accepted=1"
      ");"
      "COMMIT;";
  char* msg = nullptr;
  const int rc = sqlite3_exec(database_, sql, nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    sqlite3_free(msg);
    sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
    return support::Result<void>::failure(make_resource("m1.store.recompute"));
  }
  return support::Result<void>::success();
}

support::Result<GcPlan> MetadataStore::plan_gc(const GcPolicy& policy, bool dry_run, std::uint64_t now_ms) const {
  if (database_ == nullptr) return support::Result<GcPlan>::failure(make_invalid("m1.store.closed"));
  if (policy.require_explicit) return support::Result<GcPlan>::failure(make_invalid("m1.gc.no_policy"));

  const std::uint64_t eligible_before = now_ms > policy.grace_period_ms ? now_ms - policy.grace_period_ms : 0U;

  sqlite3_stmt* stmt = nullptr;
  // Select rows: VERIFIED, zero refs, no active lease, created before grace period
  const char* sql =
      "SELECT chunk_hash, plaintext_length, store_relpath FROM chunk_catalog"
      " WHERE state='VERIFIED' AND manifest_ref_count=0"
      " AND created_at_ms <= ?1"
      " AND chunk_hash NOT IN ("
      "   SELECT chunk_hash FROM chunk_leases WHERE expires_at_ms > ?2"
      " )";
  if (sqlite3_prepare_v2(database_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return support::Result<GcPlan>::failure(make_resource("m1.gc.plan"));

  sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(eligible_before));
  sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(now_ms));

  GcPlan plan;
  plan.policy = policy;
  plan.is_dry_run = dry_run;

  int rc = SQLITE_ROW;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const void* hash_data = sqlite3_column_blob(stmt, 0);
    const int hash_size = sqlite3_column_bytes(stmt, 0);
    if (hash_data == nullptr || hash_size != 32) continue;
    std::array<std::byte, 32> hash_bytes{};
    std::memcpy(hash_bytes.data(), hash_data, 32U);
    const auto length = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
    const char* relpath_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const std::string relpath = relpath_text != nullptr ? relpath_text : "";
    plan.eligible.push_back({support::Sha256Digest(hash_bytes), length, relpath});
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) return support::Result<GcPlan>::failure(make_resource("m1.gc.plan"));
  return support::Result<GcPlan>::success(std::move(plan));
}

support::Result<GcReport> MetadataStore::execute_gc(const std::filesystem::path& store_root, const GcPlan& plan,
                                                    std::uint64_t now_ms) {
  if (database_ == nullptr) return support::Result<GcReport>::failure(make_invalid("m1.store.closed"));
  if (plan.is_dry_run) return support::Result<GcReport>::failure(make_invalid("m1.gc.dry_run"));
  if (plan.policy.require_explicit) return support::Result<GcReport>::failure(make_invalid("m1.gc.no_policy"));

  GcReport report;
  for (const auto& entry : plan.eligible) {
    // Phase 1: In a transaction, claim this chunk with a GC lease and double-check refs/leases
    {
      char* msg = nullptr;
      if (sqlite3_exec(database_, "BEGIN", nullptr, nullptr, &msg) != SQLITE_OK) {
        sqlite3_free(msg);
        ++report.errors;
        continue;
      }
      // Recheck: still zero refs, no active lease
      sqlite3_stmt* chk = nullptr;
      if (sqlite3_prepare_v2(
              database_,
              "SELECT 1 FROM chunk_catalog WHERE chunk_hash=?1 AND manifest_ref_count=0 AND state='VERIFIED'"
              " AND chunk_hash NOT IN (SELECT chunk_hash FROM chunk_leases WHERE expires_at_ms > ?2)",
              -1, &chk, nullptr) != SQLITE_OK) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.errors;
        continue;
      }
      sqlite3_bind_blob(chk, 1, entry.hash.bytes().data(), 32, SQLITE_TRANSIENT);
      sqlite3_bind_int64(chk, 2, static_cast<sqlite3_int64>(now_ms));
      const int still_eligible = (sqlite3_step(chk) == SQLITE_ROW) ? 1 : 0;
      sqlite3_finalize(chk);

      if (!still_eligible) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.retained;
        continue;
      }

      // Mark as being GC'd
      sqlite3_stmt* upd = nullptr;
      if (sqlite3_prepare_v2(database_, "UPDATE chunk_catalog SET state='QUARANTINED' WHERE chunk_hash=?1", -1, &upd,
                             nullptr) != SQLITE_OK) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.errors;
        continue;
      }
      sqlite3_bind_blob(upd, 1, entry.hash.bytes().data(), 32, SQLITE_TRANSIENT);
      sqlite3_step(upd);
      sqlite3_finalize(upd);

      if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, &msg) != SQLITE_OK) {
        sqlite3_free(msg);
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.errors;
        continue;
      }
    }

    // Phase 2: Delete the blob file outside the transaction
    const auto blob = chunk_path(store_root, entry.hash);
    std::error_code ec;
    std::filesystem::remove(blob, ec);
    if (ec) {
      ++report.errors;
      // Leave row as QUARANTINED (reconciliation can clean it up)
      continue;
    }
    fsync_dir(blob.parent_path());

    // Phase 3: Remove the row in a second transaction after confirming blob is gone
    {
      char* msg = nullptr;
      if (sqlite3_exec(database_, "BEGIN", nullptr, nullptr, &msg) != SQLITE_OK) {
        sqlite3_free(msg);
        ++report.errors;
        continue;
      }
      // Final recheck: still zero refs, still no live lease
      sqlite3_stmt* chk2 = nullptr;
      if (sqlite3_prepare_v2(database_,
                             "SELECT 1 FROM chunk_catalog WHERE chunk_hash=?1 AND manifest_ref_count=0"
                             " AND chunk_hash NOT IN (SELECT chunk_hash FROM chunk_leases WHERE expires_at_ms > ?2)",
                             -1, &chk2, nullptr) != SQLITE_OK) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.errors;
        continue;
      }
      sqlite3_bind_blob(chk2, 1, entry.hash.bytes().data(), 32, SQLITE_TRANSIENT);
      sqlite3_bind_int64(chk2, 2, static_cast<sqlite3_int64>(now_ms));
      const int still_ok = (sqlite3_step(chk2) == SQLITE_ROW) ? 1 : 0;
      sqlite3_finalize(chk2);

      if (still_ok) {
        sqlite3_stmt* del = nullptr;
        if (sqlite3_prepare_v2(database_, "DELETE FROM chunk_catalog WHERE chunk_hash=?1", -1, &del, nullptr) !=
            SQLITE_OK) {
          sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
          ++report.errors;
          continue;
        }
        sqlite3_bind_blob(del, 1, entry.hash.bytes().data(), 32, SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_finalize(del);
        if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, &msg) != SQLITE_OK) {
          sqlite3_free(msg);
          sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
          ++report.errors;
        } else {
          ++report.removed;
        }
      } else {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        ++report.retained;
      }
    }
  }
  return support::Result<GcReport>::success(report);
}

support::Result<void> MetadataStore::set_dirty(bool dirty) {
  if (database_ == nullptr) return support::Result<void>::failure(make_invalid("m1.store.closed"));
  // Use a key-value in schema_migrations notes column... but our schema doesn't have that.
  // Instead we use a separate single-row table using a pragma or a simple DB update.
  // Since schema_migrations is our only suitable table, we use application_version column
  // of version 1 row to store a dirty marker suffix.
  const char* sql = dirty ? "UPDATE schema_migrations SET application_version='m1-dirty' WHERE version=1"
                          : "UPDATE schema_migrations SET application_version='m1' WHERE version=1";
  char* msg = nullptr;
  const int rc = sqlite3_exec(database_, sql, nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    sqlite3_free(msg);
    return support::Result<void>::failure(make_resource("m1.store.dirty"));
  }
  return support::Result<void>::success();
}

support::Result<bool> MetadataStore::is_dirty() const {
  if (database_ == nullptr) return support::Result<bool>::failure(make_invalid("m1.store.closed"));
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(database_, "SELECT application_version FROM schema_migrations WHERE version=1", -1, &stmt,
                         nullptr) != SQLITE_OK)
    return support::Result<bool>::failure(make_resource("m1.store.dirty_check"));
  bool dirty = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* ver = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    dirty = (ver != nullptr && std::string(ver) == "m1-dirty");
  }
  sqlite3_finalize(stmt);
  return support::Result<bool>::success(dirty);
}

// ─── Blob store ──────────────────────────────────────────────────────────────

support::Result<void> install_verified_chunk(const std::filesystem::path& store_root, const Chunk& chunk) {
  if (chunk.bytes.empty()) return support::Result<void>::failure(make_invalid("m1.store.empty_chunk"));
  const auto actual = sha256(chunk.bytes);
  if (!actual || *actual.value_if() != chunk.digest)
    return support::Result<void>::failure(make_invalid("m1.store.hash"));

  const auto destination = chunk_path(store_root, chunk.digest);
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) return support::Result<void>::failure(make_resource("m1.store.mkdir"));

  if (std::filesystem::exists(destination, error)) {
    // Idempotent: verify the existing blob is intact
    const auto existing = read_verified_chunk(store_root, chunk.digest);
    return existing ? support::Result<void>::success() : support::Result<void>::failure(existing.error());
  }

  // Write to unpredictable temp name to prevent partial-write exposure
  const auto temp_name = "." + random_hex_suffix() + ".tmp";
  const auto temporary = destination.parent_path() / temp_name;

  {
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return support::Result<void>::failure(make_resource("m1.store.temp"));
    std::size_t written = 0U;
    bool ok = true;
    while (written < chunk.bytes.size()) {
      const ssize_t n = ::write(fd, chunk.bytes.data() + written, chunk.bytes.size() - written);
      if (n <= 0) {
        ok = false;
        break;
      }
      written += static_cast<std::size_t>(n);
    }
    ok = ok && fsync_fd(fd);
    ::close(fd);
    if (!ok) {
      std::filesystem::remove(temporary, error);
      return support::Result<void>::failure(make_resource("m1.store.write"));
    }
  }

  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return support::Result<void>::failure(make_resource("m1.store.rename"));
  }

  // fsync the directory so the new directory entry is durable
  if (!fsync_dir(destination.parent_path())) return support::Result<void>::failure(make_resource("m1.store.fsync"));

  return support::Result<void>::success();
}

support::Result<std::vector<std::byte>> read_verified_chunk(const std::filesystem::path& store_root,
                                                            const support::Sha256Digest& digest) {
  const auto path = chunk_path(store_root, digest);
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return support::Result<std::vector<std::byte>>::failure(make_resource("m1.store.missing"));
  std::vector<std::byte> bytes;
  {
    std::array<char, 65536> buf{};
    ssize_t n = 0;
    while ((n = ::read(fd, buf.data(), buf.size())) > 0)
      for (ssize_t i = 0; i < n; ++i) bytes.push_back(static_cast<std::byte>(buf[static_cast<std::size_t>(i)]));
    ::close(fd);
    if (n < 0) return support::Result<std::vector<std::byte>>::failure(make_resource("m1.store.read_err"));
  }
  const auto actual = sha256(bytes);
  if (!actual || *actual.value_if() != digest)
    return support::Result<std::vector<std::byte>>::failure(make_invalid("m1.store.corrupt"));
  return support::Result<std::vector<std::byte>>::success(std::move(bytes));
}

// ─── Reconstruction ──────────────────────────────────────────────────────────

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
support::Result<VerifiedStagingFile> reconstruct_verified_file(const std::filesystem::path& store_root,
                                                               const std::filesystem::path& destination_root,
                                                               const ManifestFile& file) {
  const auto normalized = normalize_manifest_path(file.path);
  if (!normalized) return support::Result<VerifiedStagingFile>::failure(normalized.error());
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(destination_root, error);
  if (error || !std::filesystem::is_directory(root, error))
    return support::Result<VerifiedStagingFile>::failure(make_resource("m1.reconstruct.root"));
  const auto final_path = root / std::filesystem::path(*normalized.value_if());
  const auto relative = final_path.lexically_relative(root);
  if (relative.empty() || *relative.begin() == "..")
    return support::Result<VerifiedStagingFile>::failure(make_invalid("m1.reconstruct.path"));
  if (std::filesystem::exists(final_path, error))
    return support::Result<VerifiedStagingFile>::failure(make_invalid("m1.reconstruct.exists"));
  std::filesystem::create_directories(final_path.parent_path(), error);
  if (error) return support::Result<VerifiedStagingFile>::failure(make_resource("m1.reconstruct.mkdir"));

  // Unique staging name using random suffix (exclusive creation, restrictive permissions)
  const auto staging = final_path.parent_path() / ("." + random_hex_suffix() + ".spoink-staging");

  const int stage_fd = ::open(staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (stage_fd < 0) return support::Result<VerifiedStagingFile>::failure(make_resource("m1.reconstruct.stage"));

  std::vector<std::byte> complete;
  std::uint64_t offset = 0U;
  bool write_ok = true;

  for (const auto& reference : file.chunks) {
    if (reference.offset != offset) {
      write_ok = false;
      break;
    }
    const auto bytes = read_verified_chunk(store_root, reference.digest);
    if (!bytes || bytes.value_if()->size() != reference.bytes.size()) {
      write_ok = false;
      break;
    }
    const std::byte* data = bytes.value_if()->data();
    const std::size_t len = bytes.value_if()->size();
    std::size_t written = 0U;
    while (written < len) {
      const ssize_t n = ::write(stage_fd, data + written, len - written);
      if (n <= 0) {
        write_ok = false;
        break;
      }
      written += static_cast<std::size_t>(n);
    }
    if (!write_ok) break;
    complete.insert(complete.end(), bytes.value_if()->begin(), bytes.value_if()->end());
    offset += len;
  }

  if (write_ok) {
    write_ok = fsync_fd(stage_fd);
  }
  ::close(stage_fd);

  if (!write_ok || offset != file.size) {
    std::filesystem::remove(staging, error);
    return support::Result<VerifiedStagingFile>::failure(make_invalid("m1.reconstruct.write"));
  }

  const auto actual = sha256(complete);
  if (!actual || *actual.value_if() != file.digest) {
    std::filesystem::remove(staging, error);
    return support::Result<VerifiedStagingFile>::failure(make_invalid("m1.reconstruct.verify"));
  }
  return support::Result<VerifiedStagingFile>::success({staging, final_path, offset, *actual.value_if()});
}

support::Result<void> finalize_verified_file(const VerifiedStagingFile& staging) {
  std::error_code error;
  if (!std::filesystem::exists(staging.staging_path, error) || std::filesystem::exists(staging.final_path, error))
    return support::Result<void>::failure(make_invalid("m1.reconstruct.finalize"));
  std::filesystem::rename(staging.staging_path, staging.final_path, error);
  if (error) return support::Result<void>::failure(make_resource("m1.reconstruct.rename"));
  // fsync the parent directory so the new directory entry is durable
  if (!fsync_dir(staging.final_path.parent_path()))
    return support::Result<void>::failure(make_resource("m1.reconstruct.fsync"));
  return support::Result<void>::success();
}

// ─── Startup reconciliation ──────────────────────────────────────────────────

support::Result<ReconciliationReport> reconcile_blob_store(const std::filesystem::path& store_root, MetadataStore* db) {
  ReconciliationReport report;
  const auto chunks = store_root / "chunks";
  std::error_code error;
  if (!std::filesystem::exists(chunks, error)) return support::Result<ReconciliationReport>::success(report);
  std::filesystem::create_directories(store_root / "quarantine", error);
  if (error) return support::Result<ReconciliationReport>::failure(make_resource("m1.reconcile.quarantine"));

  for (std::filesystem::recursive_directory_iterator it(chunks, std::filesystem::directory_options::none, error), end;
       !error && it != end; it.increment(error)) {
    const auto status = it->symlink_status(error);
    if (error) break;
    if (std::filesystem::is_directory(status)) continue;
    if (!std::filesystem::is_regular_file(status))
      return support::Result<ReconciliationReport>::failure(make_invalid("m1.reconcile.entry"));
    const std::string name = it->path().filename().string();
    const auto expected = support::Sha256Digest::from_hex(name);
    bool valid = false;
    if (expected) {
      const auto bytes = read_verified_chunk(store_root, *expected.value_if());
      valid = bytes.has_value();
      if (valid && db != nullptr) {
        // Ensure DB row reflects verified state
        const auto existing = db->is_verified(*expected.value_if());
        if (existing && !*existing.value_if()) {
          // Blob is valid but row is missing/not verified: adopt it
          const auto bsize = bytes.value_if()->size();
          static_cast<void>(db->record_verified(*expected.value_if(), bsize));
        }
      }
    }
    if (!valid) {
      if (db != nullptr && expected) {
        static_cast<void>(db->mark_quarantined(*expected.value_if(), "reconcile.corrupt"));
      }
      const auto quarantine = store_root / "quarantine" / (name + ".corrupt");
      std::filesystem::rename(it->path(), quarantine, error);
      if (error) return support::Result<ReconciliationReport>::failure(make_resource("m1.reconcile.move"));
      ++report.quarantined_blobs;
    } else {
      ++report.verified_blobs;
    }
  }
  return error ? support::Result<ReconciliationReport>::failure(make_resource("m1.reconcile.scan"))
               : support::Result<ReconciliationReport>::success(report);
}

}  // namespace spoink::offline
