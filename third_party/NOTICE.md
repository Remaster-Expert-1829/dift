# Third-Party Notices

M1 adds local offline-engine dependencies. Spoink itself does not download runtime dependencies.

| Component | Locked version / source | License | Purpose |
|---|---|---|---|
| vcpkg | `2025.10.17`, commit `74e6536215718009aae747d86d84b78376bf9e09` | MIT | Reproducible C++ dependency acquisition in manifest mode. |
| Catch2 | `3.11.0#0`, vcpkg port tree `5f7385233a249ddbcc48f261b4e4c20f42297360` | BSL-1.0 | Unit-test framework; test-only. |
| OpenSSL | `3.6.0#0` | Apache-2.0 | SHA-256 adapter. |
| SQLite | `3.51.2#0` | Public Domain | Local metadata schema and transactions. |
| utf8proc | `2.11.3#0` | MIT | UTF-8 validation and NFC normalization. |

The supported CI image supplies the compiler and build tools: Ubuntu 24.04, CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, and LLVM/Clang/clang-tidy/clang-format 18.1.3. CMake is BSD-3-Clause, Ninja is Apache-2.0, GCC is GPL-3.0-or-later with the GCC Runtime Library Exception, and LLVM/Clang is Apache-2.0 WITH LLVM-exception. CI parses and compares each installed version for exact equality; a working local build on a newer tool is not a reviewed M0 lock.

Dependency updates require updating the vcpkg baseline/override, this notice, the engineering and operations documentation, and the dependency validation result together.
