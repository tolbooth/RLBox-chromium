#!/usr/bin/env python3
# Copyright 2025 tolbooth
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Transpile WASM to C using wasm2c."""

import argparse
import subprocess
import sys


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--wasm2c', required=True)
  parser.add_argument('--input', required=True)
  parser.add_argument('--output', required=True)
  parser.add_argument('--module-name', required=True)
  args = parser.parse_args()

  cmd = [args.wasm2c, '-n', args.module_name, '-o', args.output, args.input]
  print(' '.join(cmd), file=sys.stderr)
  result = subprocess.run(cmd, capture_output=True, text=True)
  if result.returncode != 0:
    print(result.stderr, file=sys.stderr)
  return result.returncode


if __name__ == '__main__':
  sys.exit(main())
