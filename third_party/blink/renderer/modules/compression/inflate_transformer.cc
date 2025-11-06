// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/compression/inflate_transformer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "base/compiler_specific.h"
#include "base/trace_event/typed_macros.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_typedefs.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_arraybuffer_arraybufferview.h"
#include "third_party/blink/renderer/core/streams/transform_stream_default_controller.h"
#include "third_party/blink/renderer/core/streams/transform_stream_transformer.h"
#include "third_party/blink/renderer/core/typed_arrays/array_buffer_view_helpers.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_piece.h"
#include "third_party/blink/renderer/modules/compression/compression_format.h"
#include "third_party/blink/renderer/modules/compression/zlib_rlbox_types.h"
#include "third_party/blink/renderer/modules/compression/zlib_structs_for_rlbox.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "v8/include/v8.h"

// Load struct field definitions for sandboxed z_stream access
rlbox_load_structs_from_library(zlib);

namespace blink {

InflateTransformer::InflateTransformer(
    ScriptState* script_state,
    CompressionFormat format,
    rlbox_sandbox_zlib* sandbox)
    : script_state_(script_state),
      sandbox_(sandbox),
      sandboxed_stream_(nullptr),
      sandboxed_out_buffer_(nullptr),
      sandboxed_in_buffer_(nullptr) {
  DCHECK(sandbox_);

  sandboxed_stream_ = sandbox_->malloc_in_sandbox<z_stream>();
  sandboxed_out_buffer_ = sandbox_->malloc_in_sandbox<uint8_t>(kBufferSize);

  // Set zalloc/zfree to Z_NULL to use default allocators within the sandbox
  z_stream temp_stream = {};
  temp_stream.zalloc = Z_NULL;
  temp_stream.zfree = Z_NULL;
  temp_stream.opaque = Z_NULL;

  // Copy initialized stream to sandbox
  rlbox::memcpy(*sandbox_, sandboxed_stream_, &temp_stream, sizeof(z_stream));

  // Compression format determines the number of window bits (base 2 of window size)
  constexpr int kWindowBits = 15;
  constexpr int kUseGzip = 16;
  int window_bits;
  switch (format) {
    case CompressionFormat::kDeflate:
      window_bits = kWindowBits;
      break;
    case CompressionFormat::kGzip:
      window_bits = kWindowBits + kUseGzip;
      break;
    case CompressionFormat::kDeflateRaw:
      window_bits = -kWindowBits;
      break;
  }

  // call inflateInit2_() in sandbox
  // Allocate version string in sandbox memory
  const char* version_str = ZLIB_VERSION;
  size_t version_len = std::strlen(version_str) + 1;  // Include null terminator
  auto sandboxed_version = sandbox_->malloc_in_sandbox<char>(version_len);
  rlbox::strncpy(*sandbox_, sandboxed_version, version_str, version_len);

  auto result = sandbox_->invoke_sandbox_function(
      inflateInit2_, sandboxed_stream_, window_bits, sandboxed_version,
      static_cast<int>(sizeof(z_stream)));
  int err = result.unverified_safe_because(
      "Error code from inflateInit2. App handles all zlib error codes safely.");

  sandbox_->free_in_sandbox(sandboxed_version);
  DCHECK_EQ(Z_OK, err);
}

InflateTransformer::~InflateTransformer() {
  if (sandboxed_stream_) {
    if (!was_flush_called_) {
      sandbox_->invoke_sandbox_function(inflateEnd, sandboxed_stream_);
    }
    sandbox_->free_in_sandbox(sandboxed_stream_);
  }

  if (sandboxed_out_buffer_) {
    sandbox_->free_in_sandbox(sandboxed_out_buffer_);
  }

  if (sandboxed_in_buffer_) {
    sandbox_->free_in_sandbox(sandboxed_in_buffer_);
  }
}

ScriptPromise<IDLUndefined> InflateTransformer::Transform(
    v8::Local<v8::Value> chunk,
    TransformStreamDefaultController* controller,
    ExceptionState& exception_state) {
  auto* buffer_source = V8BufferSource::Create(script_state_->GetIsolate(),
                                               chunk, exception_state);
  if (exception_state.HadException())
    return EmptyPromise();
  DOMArrayPiece array_piece(buffer_source);
  if (array_piece.ByteLength() > std::numeric_limits<wtf_size_t>::max()) {
    exception_state.ThrowRangeError(
        "Buffer size exceeds maximum heap object size.");
    return EmptyPromise();
  }
  Inflate(array_piece.Bytes(),
          static_cast<wtf_size_t>(array_piece.ByteLength()), IsFinished(false),
          controller, exception_state);
  return ToResolvedUndefinedPromise(script_state_.Get());
}

ScriptPromise<IDLUndefined> InflateTransformer::Flush(
    TransformStreamDefaultController* controller,
    ExceptionState& exception_state) {
  DCHECK(!was_flush_called_);
  was_flush_called_ = true;
  Inflate(nullptr, 0u, IsFinished(true), controller, exception_state);
  sandbox_->invoke_sandbox_function(inflateEnd, sandboxed_stream_);

  if (exception_state.HadException()) {
    return EmptyPromise();
  }

  if (!reached_end_) {
    exception_state.ThrowTypeError("Compressed input was truncated.");
  }

  return ToResolvedUndefinedPromise(script_state_.Get());
}

void InflateTransformer::Inflate(const uint8_t* start,
                                 wtf_size_t length,
                                 IsFinished finished,
                                 TransformStreamDefaultController* controller,
                                 ExceptionState& exception_state) {
  TRACE_EVENT("blink,devtools.timeline", "DecompressionStream Inflate");
  if (reached_end_ && length != 0) {
    // zlib will ignore data after the end of the stream, so we have to
    // explicitly throw an error.
    exception_state.ThrowTypeError("Junk found after end of compressed data.");
    return;
  }

  // Allocate input buffer in sandbox and copy data if needed
  if (length > 0) {
    // Free old buffer if it exists
    if (sandboxed_in_buffer_) {
      sandbox_->free_in_sandbox(sandboxed_in_buffer_);
    }

    sandboxed_in_buffer_ = sandbox_->malloc_in_sandbox<uint8_t>(length);
    rlbox::memcpy(*sandbox_, sandboxed_in_buffer_, start, length);
  }

  // Set sandboxed stream input fields
  sandboxed_stream_->next_in = length > 0 ? sandboxed_in_buffer_ : nullptr;
  sandboxed_stream_->avail_in = length;

  // enqueue() may execute JavaScript which may invalidate the input buffer. So
  // accumulate all the output before calling enqueue().
  HeapVector<Member<DOMUint8Array>, 1u> buffers;

  uInt avail_out = 0;
  do {
    // Set sandboxed stream output fields
    sandboxed_stream_->avail_out = kBufferSize;
    sandboxed_stream_->next_out = sandboxed_out_buffer_;

    // Call inflate() in sandbox
    auto result = sandbox_->invoke_sandbox_function(
        inflate, sandboxed_stream_, finished ? Z_FINISH : Z_NO_FLUSH);
    const int err = result.unverified_safe_because(
        "Error code from inflate. We handle all zlib error codes safely.");

    if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
      DCHECK_NE(err, Z_STREAM_ERROR);

      EnqueueBuffers(controller, std::move(buffers), exception_state);
      if (exception_state.HadException()) {
        return;
      }

      if (err == Z_DATA_ERROR) {
        // Read error message from sandbox
        String error_msg = "The compressed data was not valid";

        auto verified_msg = sandboxed_stream_->msg.copy_and_verify_string(
            [](std::unique_ptr<char[]> val) {
              if (val) {
                size_t len = std::strlen(val.get());
                // Further length validation. Zlib error messages tend to be
                // a handful of characters, so large messages may be malicious.
                if (len > 0 && len < 256) {
                  return val;
                }
              }
              return std::unique_ptr<char[]>(nullptr);
            });

        if (verified_msg) {
          error_msg = error_msg + ": " +
                     String(verified_msg.get());
        }
        exception_state.ThrowTypeError(error_msg + ".");
      } else {
        exception_state.ThrowTypeError("The compressed data was not valid.");
      }
      return;
    }

    // Read how many bytes were written to output buffer
    // Verify avail_out since we use it for bounds calculations
    avail_out = sandboxed_stream_->avail_out.copy_and_verify([](uInt val) {
      return val <= kBufferSize ? val : kBufferSize;
    });

    wtf_size_t bytes = kBufferSize - avail_out;
    if (bytes) {
      // Buffer overflow protection is provided by validating avail_out before
      // calculating bytes.
      auto verified_buffer = sandboxed_out_buffer_.unverified_safe_pointer_because(
          bytes,
          "Decompressed data is inherently arbitrary and cannot be validated "
          "without knowing the expected format."
      );

      if (verified_buffer) {
        // Create DOMUint8Array and copy from sandbox in one step
        auto *dom_array = DOMUint8Array::Create(bytes);
        if (dom_array) {
          std::copy_n(verified_buffer, bytes, dom_array->Data());
          buffers.push_back(dom_array);
        } else {
          exception_state.ThrowTypeError("Failed to allocate decompression buffer.");
          return;
        }
      } else {
        // Sandbox returned invalid buffer, something went wrong
        exception_state.ThrowTypeError("Internal decompression error.");
        return;
      }
    }

    if (err == Z_STREAM_END) {
      reached_end_ = true;

      // Check if there's junk data after the compressed stream
      const bool junk_found =
          sandboxed_stream_->avail_in.unverified_safe_because(
              "Used for comparison only. App is robust to any value.") > 0;

      EnqueueBuffers(controller, std::move(buffers), exception_state);
      if (exception_state.HadException()) {
        return;
      }

      if (junk_found) {
        exception_state.ThrowTypeError(
            "Junk found after end of compressed data.");
      }
      return;
    }
  } while (avail_out == 0);

  auto remaining_in = sandboxed_stream_->avail_in.copy_and_verify([length](uInt val) {
    return val <= length ? val : 0u;
  });
  DCHECK_EQ(remaining_in, 0u);

  EnqueueBuffers(controller, std::move(buffers), exception_state);
}

void InflateTransformer::EnqueueBuffers(
    TransformStreamDefaultController* controller,
    HeapVector<Member<DOMUint8Array>, 1u> buffers,
    ExceptionState& exception_state) {
  // JavaScript may be executed inside this loop, however it is safe because
  // |buffers| is a local variable that JavaScript cannot modify.
  for (DOMUint8Array* buffer : buffers) {
    controller->enqueue(script_state_, ScriptValue::From(script_state_, buffer),
                        exception_state);
    if (exception_state.HadException()) {
      return;
    }
  }
}

void InflateTransformer::Trace(Visitor* visitor) const {
  visitor->Trace(script_state_);
  TransformStreamTransformer::Trace(visitor);
}

}  // namespace blink
