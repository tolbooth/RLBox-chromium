// Copyright 2025 tolbooth Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Struct field reflection for RLBox sandboxing of zlib
// Based on z_stream definition in third_party/zlib/zlib.h:86-106
//
// This provides manual struct reflection for RLBox to enable accessing
// tainted struct fields without untainting the entire struct.
// See: https://shravanrn.com/oldrlboxdocs/#operating-on-tainted-values

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_STRUCTS_FOR_RLBOX_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_STRUCTS_FOR_RLBOX_H_

#define sandbox_fields_reflection_zlib_class_z_stream(f, g, ...)        \
  f(z_const Bytef *, next_in, FIELD_NORMAL, ##__VA_ARGS__) g()          \
  f(uInt, avail_in, FIELD_NORMAL, ##__VA_ARGS__) g()                    \
  f(uLong, total_in, FIELD_NORMAL, ##__VA_ARGS__) g()                   \
  f(Bytef *, next_out, FIELD_NORMAL, ##__VA_ARGS__) g()                 \
  f(uInt, avail_out, FIELD_NORMAL, ##__VA_ARGS__) g()                   \
  f(uLong, total_out, FIELD_NORMAL, ##__VA_ARGS__) g()                  \
  f(z_const char *, msg, FIELD_NORMAL, ##__VA_ARGS__) g()               \
  f(struct internal_state *, state, FIELD_NORMAL, ##__VA_ARGS__) g()    \
  f(alloc_func, zalloc, FIELD_NORMAL, ##__VA_ARGS__) g()                \
  f(free_func, zfree, FIELD_NORMAL, ##__VA_ARGS__) g()                  \
  f(voidpf, opaque, FIELD_NORMAL, ##__VA_ARGS__) g()                    \
  f(int, data_type, FIELD_NORMAL, ##__VA_ARGS__) g()                    \
  f(uLong, adler, FIELD_NORMAL, ##__VA_ARGS__) g()                      \
  f(uLong, reserved, FIELD_NORMAL, ##__VA_ARGS__) g()

// Define the list of all structs for this library
#define sandbox_fields_reflection_zlib_allClasses(f, ...) \
  f(z_stream, zlib, ##__VA_ARGS__)

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_STRUCTS_FOR_RLBOX_H_
