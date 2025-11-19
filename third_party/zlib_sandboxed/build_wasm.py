#!/usr/bin/env python3
# Copyright 2025 tolbooth
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Compile C sources to WebAssembly using wasi-clang."""

import argparse
import subprocess
import sys


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--wasi-clang', required=True)
  parser.add_argument('--sysroot', required=True)
  parser.add_argument('--output', required=True)
  parser.add_argument('--include-dir', required=True)
  parser.add_argument('sources', nargs='+')
  args = parser.parse_args()

  cmd = [
      args.wasi_clang,
      '--target=wasm32-wasi',
      f'--sysroot={args.sysroot}',
      '-O2',
      f'-I{args.include_dir}',
      # Disable Chromium's Cr_z_ symbol prefixing for WASM build
      # The sandbox will access zlib functions by their original names
      '-DTHIRD_PARTY_ZLIB_CHROMECONF_H_',
  ]
  cmd.extend(args.sources)
  cmd.extend([
      '-Wl,--export-all',
      '-Wl,--no-entry',
      '-Wl,--growable-table',
      '-Wl,--stack-first,-z,stack-size=1048576',
      '-o', args.output,
  ])

  print(' '.join(cmd), file=sys.stderr)
  result = subprocess.run(cmd, capture_output=True, text=True)
  if result.returncode != 0:
    print(result.stderr, file=sys.stderr)
  return result.returncode


if __name__ == '__main__':
  sys.exit(main())
