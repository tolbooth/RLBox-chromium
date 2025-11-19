// Copyright 2025 tolbooth
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_

// RLBox configuration. These must be defined before rlbox includes
#define RLBOX_SINGLE_THREADED_INVOCATIONS
#define RLBOX_WASM2C_MODULE_NAME zlib
#define RLBOX_USE_STATIC_CALLS() rlbox_wasm2c_sandbox_lookup_symbol
// wasm2c generates unmangled symbols (w2c_zlib_*), so turn off mangling 
#define RLBOX_WASM2C_MANGLED_MODULE_NAME() RLBOX_WASM2C_MODULE_NAME

// Include generated wasm2c header. This has to come before rlbox_wasm2c_sandbox.hpp
#include "third_party/zlib_sandboxed/zlib.wasm.h"

// wasm2c hex-encodes trailing underscores in export names (0x5F = '_')
// Create macro aliases so RLBox's lookup finds the correct symbols
#define w2c_zlib_deflateInit2_ w2c_zlib_deflateInit20x5F
#define w2c_zlib_inflateInit2_ w2c_zlib_inflateInit20x5F

#include "third_party/rlbox/src/code/include/rlbox.hpp"
#include "third_party/rlbox_wasm2c_sandbox/src/include/rlbox_wasm2c_sandbox.hpp"

// Define base types for zlib library using the wasm2c sandbox
// Must be defined exactly once for the entire program
RLBOX_DEFINE_BASE_TYPES_FOR(zlib, wasm2c)

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_
