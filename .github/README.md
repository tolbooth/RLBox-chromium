This is an experimental fork of Chromium, and as such is NOT intended for use as
a browser. 

# Setup Guide

## Prerequisites

- Standard Chromium build dependencies (see https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md)
- Git
- CMake (for building wasm2c tool)
- ~20GB disk space

## Setup Steps

### 1. Clone the Repository

First, follow the above guide to checkout chromium.

Then, add this fork as remote, and checkout main.

### 2. Initialize Git Submodules

```bash
# Initialize and update ONLY RLBox and wabt submodules (not all Chromium submodules)
git submodule update --init --recursive third_party/rlbox/src third_party/wabt/src
```

This will:
- Clone RLBox to `third_party/rlbox/src/`
- Clone wabt to `third_party/wabt/src/`
- Initialize wabt's own submodules (picosha2, gtest, etc.)

### 3. Download wasi-sdk Prebuilt Binaries

Download the wasi-sdk prebuilt release (required for compiling C to WebAssembly):

```bash
cd third_party/wasi-sdk

# Download wasi-sdk v24.0 for Linux x86_64
wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/wasi-sdk-24.0-x86_64-linux.tar.gz

# Extract directly into this directory
tar -xzf wasi-sdk-24.0-x86_64-linux.tar.gz --strip-components=1

# Clean up tarball
rm wasi-sdk-24.0-x86_64-linux.tar.gz

# Return to root
cd ../..
```

### 4. Build wasm2c Tool

The wasm2c tool needs to be built from source:

```bash
cd third_party/wabt/src

# Create build directory
mkdir -p build && cd build

# Configure with CMake (disable tests for faster build)
cmake .. -DBUILD_TESTS=OFF

# Build just the wasm2c target
cmake --build . --target wasm2c

# Return to chromium root
cd ../../../..
```

Now copy the built wasm2c binary to the expected location:

```bash
mkdir -p third_party/wabt/bin
cp third_party/wabt/src/build/wasm2c third_party/wabt/bin/
chmod +x third_party/wabt/bin/wasm2c
```

Verify wasm2c works:
```bash
third_party/wabt/bin/wasm2c --version
```

### 5. Configure and Build Chromium

Generate the build configuration:

```bash
gn gen out/RLBox
```

Build content_shell (this will take a while on first build):

```bash
autoninja -C out/RLBox content_shell
```

Once built, run:

```bash
out/RLBox/content_shell
```

## Additional Resources

- **RLBox Documentation**: https://rlbox.dev/
- **Firefox RLBox Integration** (reference implementation): https://searchfox.org/mozilla-central/search?q=rlbox&path=
- **Chromium Build System**: https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md
- **WASI SDK**: https://github.com/WebAssembly/wasi-sdk
- **wabt (WebAssembly Binary Toolkit)**: https://github.com/WebAssembly/wabt
