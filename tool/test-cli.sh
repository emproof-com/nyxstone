#!/bin/sh

set -eu

# Sanity check that assembling and disassembling yields the same output
# NOTE: Currently we cannot test using labels, since we keep them when assembling.
assembly="cmp rax, rbx; inc rax; add rsp, 8; ret"
address="0xdeadbeef"

assembled=$(./nyxstone -t "x86_64" -p "$address" "$assembly")
assembled_bytes=$(./nyxstone -t "x86_64" --bytes-only -p "$address" "$assembly")
# assembled_bytes="03 02"
disassembled=$(./nyxstone -t "x86_64" -p "$address" -d "$assembled_bytes")

if [ "$assembled" = "$disassembled" ]; then
  exit 0
else
  echo "Output Mismatch"
  echo "---------------"
  echo "Assembled:"
  echo "$assembled"
  echo "Disassembled:"
  echo "$disassembled"
  exit 1
fi
