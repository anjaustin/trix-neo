#!/usr/bin/env python3
"""
6502 ALU Visualization - Proof of Concept Demo

This visualizes the 6502 ALU operations.
This is a DEMO, not core TriX.

Usage:
    python viz_6502.py ADC 42 13
    python viz_6502.py EOR 0x55 0xFF
    python viz_6502.py ASL 0x80
"""

import sys
import os

# Add viz directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'viz'))

from viz_core import (
    Color, Frame,
    fmt_float, fmt_binary, fmt_hex,
)


# ============================================================================
# 6502 ALU IMPLEMENTATION
# ============================================================================

def frozen_xor(a: float, b: float) -> float:
    return a + b - 2.0 * a * b

def frozen_and(a: float, b: float) -> float:
    return a * b

def frozen_or(a: float, b: float) -> float:
    return a + b - a * b

def frozen_full_adder(a, b, cin):
    ab_xor = frozen_xor(a, b)
    sum_out = frozen_xor(ab_xor, cin)
    ab_and = frozen_and(a, b)
    xor_cin_and = frozen_and(ab_xor, cin)
    carry_out = frozen_or(ab_and, xor_cin_and)
    return sum_out, carry_out

def frozen_ripple_add(a: int, b: int, cin: int = 0):
    carry = float(cin)
    result = 0
    for i in range(8):
        a_bit = float((a >> i) & 1)
        b_bit = float((b >> i) & 1)
        sum_bit, carry = frozen_full_adder(a_bit, b_bit, carry)
        if sum_bit > 0.5:
            result |= (1 << i)
    return result, (1 if carry > 0.5 else 0)


ALU_OPS = {
    'ADC': 0, 'SBC': 1, 'AND': 2, 'ORA': 3, 'EOR': 4,
    'ASL': 5, 'LSR': 6, 'ROL': 7, 'ROR': 8, 'INC': 9, 'DEC': 10,
}

SHAPE_NAMES = {
    0: 'RIPPLE_ADD', 1: 'RIPPLE_SUB', 2: 'PARALLEL_AND',
    3: 'PARALLEL_OR', 4: 'PARALLEL_XOR', 5: 'SHIFT_LEFT',
    6: 'SHIFT_RIGHT', 7: 'ROTATE_LEFT', 8: 'ROTATE_RIGHT',
    9: 'INCREMENT', 10: 'DECREMENT',
}


def alu_execute(op: str, a: int, b: int, cin: int = 0):
    """Execute 6502 ALU operation."""
    op = op.upper()
    op_id = ALU_OPS.get(op, 0)
    shape_name = SHAPE_NAMES[op_id]

    if op == 'ADC':
        result, cout = frozen_ripple_add(a, b, cin)
    elif op == 'SBC':
        result, cout = frozen_ripple_add(a, b ^ 0xFF, 1 - cin)
        cout = 1 - cout
    elif op == 'AND':
        result, cout = a & b, 0
    elif op == 'ORA':
        result, cout = a | b, 0
    elif op == 'EOR':
        result, cout = a ^ b, 0
    elif op == 'ASL':
        cout = (a >> 7) & 1
        result = (a << 1) & 0xFF
    elif op == 'LSR':
        cout = a & 1
        result = a >> 1
    elif op == 'ROL':
        cout = (a >> 7) & 1
        result = ((a << 1) | cin) & 0xFF
    elif op == 'ROR':
        cout = a & 1
        result = (a >> 1) | (cin << 7)
    elif op == 'INC':
        result, cout = (a + 1) & 0xFF, 0
    elif op == 'DEC':
        result, cout = (a - 1) & 0xFF, 0
    else:
        result, cout = a, 0

    return result, cout, shape_name


def trace_alu(op: str, a: int, b: int, cin: int = 0):
    """Trace 6502 ALU operation."""
    result, cout, shape_name = alu_execute(op, a, b, cin)

    frame = Frame(f"6502 ALU: {op.upper()} (Proof of Concept)", width=56)

    frame.add_section("INPUT", [
        f"  opcode  = {Color.value(op.upper())}",
        f"  A       = {Color.value(fmt_hex(a))}  ({a})",
        f"  operand = {Color.value(fmt_hex(b))}  ({b})" if op.upper() not in ['ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'] else f"  operand = (none)",
        f"  carry   = {Color.value(str(cin))}",
    ])

    frame.add_section("ROUTE", [
        f"  opcode {Color.dim('→')} shape: {Color.formula(shape_name)}",
    ])

    if op.upper() in ['ADC', 'SBC']:
        frame.add_section("COMPUTE", [
            f"  8-bit ripple {'add' if op.upper() == 'ADC' else 'subtract'}",
            f"  {a} {'+' if op.upper() == 'ADC' else '-'} {b} {'+' if op.upper() == 'ADC' else '-'} {cin}",
        ])
    elif op.upper() in ['AND', 'ORA', 'EOR']:
        op_sym = {'AND': '&', 'ORA': '|', 'EOR': '^'}[op.upper()]
        frame.add_section("COMPUTE", [
            f"  a:      {fmt_binary(a)}",
            f"  b:      {fmt_binary(b)}",
            f"  {op_sym}       {'─' * 8}",
            f"  result: {fmt_binary(result)}",
        ])
    elif op.upper() in ['ASL', 'LSR']:
        direction = "left" if op.upper() == 'ASL' else "right"
        frame.add_section("COMPUTE", [
            f"  shift {direction}",
            f"  before: {fmt_binary(a)}",
            f"  after:  {fmt_binary(result)}  C={cout}",
        ])
    elif op.upper() in ['ROL', 'ROR']:
        direction = "left" if op.upper() == 'ROL' else "right"
        frame.add_section("COMPUTE", [
            f"  rotate {direction} through carry",
            f"  before: {fmt_binary(a)}  C={cin}",
            f"  after:  {fmt_binary(result)}  C={cout}",
        ])
    elif op.upper() in ['INC', 'DEC']:
        delta = "+1" if op.upper() == 'INC' else "-1"
        frame.add_section("COMPUTE", [
            f"  {a} {delta} = {result}",
            f"  {fmt_binary(a)} → {fmt_binary(result)}",
        ])

    frame.add_section("OUTPUT", [
        f"  result = {Color.value(fmt_hex(result))}  ({result})",
        f"  carry  = {Color.value(str(cout))}",
        f"  bits   = {fmt_binary(result)}",
    ])

    print()
    frame.display()
    print()
    print(Color.dim("  This is a proof of concept, not core TriX."))
    print()


def parse_value(s: str) -> int:
    s = s.strip()
    if s.startswith('0x') or s.startswith('0X'):
        return int(s, 16)
    return int(s)


def main():
    if len(sys.argv) < 3:
        print("6502 ALU Visualization - Proof of Concept")
        print()
        print("Usage: python viz_6502.py <OP> <a> [b] [carry]")
        print()
        print("Operations:")
        print("  ADC SBC AND ORA EOR ASL LSR ROL ROR INC DEC")
        print()
        print("Examples:")
        print("  python viz_6502.py ADC 42 13")
        print("  python viz_6502.py EOR 0x55 0xFF")
        print("  python viz_6502.py ASL 0x80")
        print()
        print("This is a DEMO, not core TriX.")
        sys.exit(1)

    op = sys.argv[1].upper()
    a = parse_value(sys.argv[2])
    b = parse_value(sys.argv[3]) if len(sys.argv) > 3 else 0
    cin = int(sys.argv[4]) if len(sys.argv) > 4 else 0

    if op not in ALU_OPS:
        print(f"Unknown operation: {op}")
        print("Supported: ADC SBC AND ORA EOR ASL LSR ROL ROR INC DEC")
        sys.exit(1)

    trace_alu(op, a, b, cin)


if __name__ == '__main__':
    main()
