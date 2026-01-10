#!/usr/bin/env python3
"""
TriX Visualization - Single-Step Trace

Trace a single frozen shape operation step by step.
The MVP of TriX visualization.

Usage:
    python viz_trace.py xor 0 1
    python viz_trace.py and 1 1
    python viz_trace.py or 0 1
    python viz_trace.py not 1
    python viz_trace.py add 1 1 0        # full adder: a, b, cin
    python viz_trace.py ripple 42 13     # 8-bit ripple add
    python viz_trace.py alu ADC 42 13    # 6502 ALU operation
    python viz_trace.py alu EOR 0x55 0xFF

Options:
    --auto      Auto-advance with animation
    --instant   No pauses, just show final result
    --step      Step-by-step (default)

"If you can show it, there's no magic."
"""

import sys
import argparse
from typing import List, Tuple, Optional

from viz_core import (
    Color, Box, Frame, ComputeStep,
    print_lines, pause, animate_delay, clear_screen,
    fmt_float, fmt_binary, fmt_hex, fmt_result,
    render_bits, render_computation,
)


# ============================================================================
# FROZEN SHAPES - Pure Python implementations matching C
# ============================================================================

def frozen_xor(a: float, b: float) -> float:
    """XOR(a, b) = a + b - 2ab"""
    return a + b - 2.0 * a * b


def frozen_and(a: float, b: float) -> float:
    """AND(a, b) = ab"""
    return a * b


def frozen_or(a: float, b: float) -> float:
    """OR(a, b) = a + b - ab"""
    return a + b - a * b


def frozen_not(a: float) -> float:
    """NOT(a) = 1 - a"""
    return 1.0 - a


def frozen_nand(a: float, b: float) -> float:
    """NAND(a, b) = 1 - ab"""
    return 1.0 - a * b


def frozen_nor(a: float, b: float) -> float:
    """NOR(a, b) = 1 - a - b + ab"""
    return 1.0 - a - b + a * b


def frozen_xnor(a: float, b: float) -> float:
    """XNOR(a, b) = 1 - a - b + 2ab"""
    return 1.0 - a - b + 2.0 * a * b


def frozen_full_adder(a: float, b: float, cin: float) -> Tuple[float, float]:
    """Full adder: returns (sum, carry)"""
    # sum = a XOR b XOR cin
    ab_xor = frozen_xor(a, b)
    sum_out = frozen_xor(ab_xor, cin)

    # carry = (a AND b) OR ((a XOR b) AND cin)
    ab_and = frozen_and(a, b)
    xor_cin_and = frozen_and(ab_xor, cin)
    carry_out = frozen_or(ab_and, xor_cin_and)

    return sum_out, carry_out


def frozen_ripple_add(a: int, b: int, cin: int = 0) -> Tuple[int, int, List[int]]:
    """8-bit ripple carry adder. Returns (result, carry_out, carry_chain)"""
    carry = float(cin)
    result = 0
    carry_chain = []

    for i in range(8):
        a_bit = float((a >> i) & 1)
        b_bit = float((b >> i) & 1)

        sum_bit, carry = frozen_full_adder(a_bit, b_bit, carry)

        if sum_bit > 0.5:
            result |= (1 << i)
        carry_chain.append(1 if carry > 0.5 else 0)

    return result, (1 if carry > 0.5 else 0), carry_chain


# ============================================================================
# 6502 ALU
# ============================================================================

ALU_OPS = {
    'ADC': 0,   # Add with carry
    'SBC': 1,   # Subtract with borrow
    'AND': 2,   # Bitwise AND
    'ORA': 3,   # Bitwise OR
    'EOR': 4,   # Exclusive OR
    'ASL': 5,   # Arithmetic shift left
    'LSR': 6,   # Logical shift right
    'ROL': 7,   # Rotate left
    'ROR': 8,   # Rotate right
    'INC': 9,   # Increment
    'DEC': 10,  # Decrement
}

SHAPE_NAMES = {
    0: 'RIPPLE_ADD',
    1: 'RIPPLE_SUB',
    2: 'PARALLEL_AND',
    3: 'PARALLEL_OR',
    4: 'PARALLEL_XOR',
    5: 'SHIFT_LEFT',
    6: 'SHIFT_RIGHT',
    7: 'ROTATE_LEFT',
    8: 'ROTATE_RIGHT',
    9: 'INCREMENT',
    10: 'DECREMENT',
}


def alu_execute(op: str, a: int, b: int, cin: int = 0) -> Tuple[int, int, str]:
    """Execute 6502 ALU operation. Returns (result, carry_out, shape_name)"""
    op = op.upper()
    if op not in ALU_OPS:
        raise ValueError(f"Unknown ALU op: {op}")

    op_id = ALU_OPS[op]
    shape_name = SHAPE_NAMES[op_id]

    if op == 'ADC':
        result, cout, _ = frozen_ripple_add(a, b, cin)
    elif op == 'SBC':
        # SBC uses inverted carry (borrow)
        result, cout, _ = frozen_ripple_add(a, b ^ 0xFF, 1 - cin)
        cout = 1 - cout  # invert back
    elif op == 'AND':
        result = a & b
        cout = 0
    elif op == 'ORA':
        result = a | b
        cout = 0
    elif op == 'EOR':
        result = a ^ b
        cout = 0
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
        result = (a + 1) & 0xFF
        cout = 0
    elif op == 'DEC':
        result = (a - 1) & 0xFF
        cout = 0
    else:
        result = a
        cout = 0

    return result, cout, shape_name


# ============================================================================
# TRACE VISUALIZATIONS
# ============================================================================

def trace_xor(a: float, b: float, mode: str = 'step'):
    """Trace XOR operation."""
    result = frozen_xor(a, b)
    expected = 1.0 if (int(a) ^ int(b)) else 0.0

    frame = Frame("TRACE: XOR", width=48)

    # INPUT
    frame.add_section("INPUT", [
        f"  a = {Color.value(fmt_float(a))}",
        f"  b = {Color.value(fmt_float(b))}",
    ])

    # FORMULA
    frame.add_section("FORMULA", [
        f"  XOR(a, b) = {Color.formula('a + b - 2·a·b')}",
    ])

    # COMPUTE
    step1 = a + b
    step2 = 2.0 * a * b
    step3 = step1 - step2
    frame.add_section("COMPUTE", [
        f"  step 1:  a + b     = {fmt_float(a)} + {fmt_float(b)}",
        f"                     = {Color.value(fmt_float(step1))}",
        f"",
        f"  step 2:  2·a·b     = 2 × {fmt_float(a)} × {fmt_float(b)}",
        f"                     = {Color.value(fmt_float(step2))}",
        f"",
        f"  step 3:  subtract  = {fmt_float(step1)} - {fmt_float(step2)}",
        f"                     = {Color.value(fmt_float(step3))}",
    ])

    # OUTPUT
    frame.add_section("OUTPUT", [
        f"  result = {fmt_result(result, expected)}",
        f"  binary = {Color.bit(result)}",
    ])

    print()
    frame.display()
    print()


def trace_and(a: float, b: float, mode: str = 'step'):
    """Trace AND operation."""
    result = frozen_and(a, b)
    expected = 1.0 if (int(a) and int(b)) else 0.0

    frame = Frame("TRACE: AND", width=48)

    frame.add_section("INPUT", [
        f"  a = {Color.value(fmt_float(a))}",
        f"  b = {Color.value(fmt_float(b))}",
    ])

    frame.add_section("FORMULA", [
        f"  AND(a, b) = {Color.formula('a · b')}",
    ])

    frame.add_section("COMPUTE", [
        f"  a × b = {fmt_float(a)} × {fmt_float(b)}",
        f"        = {Color.value(fmt_float(result))}",
    ])

    frame.add_section("OUTPUT", [
        f"  result = {fmt_result(result, expected)}",
        f"  binary = {Color.bit(result)}",
    ])

    print()
    frame.display()
    print()


def trace_or(a: float, b: float, mode: str = 'step'):
    """Trace OR operation."""
    result = frozen_or(a, b)
    expected = 1.0 if (int(a) or int(b)) else 0.0

    frame = Frame("TRACE: OR", width=48)

    frame.add_section("INPUT", [
        f"  a = {Color.value(fmt_float(a))}",
        f"  b = {Color.value(fmt_float(b))}",
    ])

    frame.add_section("FORMULA", [
        f"  OR(a, b) = {Color.formula('a + b - a·b')}",
    ])

    step1 = a + b
    step2 = a * b
    step3 = step1 - step2
    frame.add_section("COMPUTE", [
        f"  step 1:  a + b   = {fmt_float(a)} + {fmt_float(b)}",
        f"                   = {Color.value(fmt_float(step1))}",
        f"",
        f"  step 2:  a · b   = {fmt_float(a)} × {fmt_float(b)}",
        f"                   = {Color.value(fmt_float(step2))}",
        f"",
        f"  step 3:  subtract = {fmt_float(step1)} - {fmt_float(step2)}",
        f"                   = {Color.value(fmt_float(step3))}",
    ])

    frame.add_section("OUTPUT", [
        f"  result = {fmt_result(result, expected)}",
        f"  binary = {Color.bit(result)}",
    ])

    print()
    frame.display()
    print()


def trace_not(a: float, mode: str = 'step'):
    """Trace NOT operation."""
    result = frozen_not(a)
    expected = 0.0 if int(a) else 1.0

    frame = Frame("TRACE: NOT", width=48)

    frame.add_section("INPUT", [
        f"  a = {Color.value(fmt_float(a))}",
    ])

    frame.add_section("FORMULA", [
        f"  NOT(a) = {Color.formula('1 - a')}",
    ])

    frame.add_section("COMPUTE", [
        f"  1 - a = 1.0 - {fmt_float(a)}",
        f"        = {Color.value(fmt_float(result))}",
    ])

    frame.add_section("OUTPUT", [
        f"  result = {fmt_result(result, expected)}",
        f"  binary = {Color.bit(result)}",
    ])

    print()
    frame.display()
    print()


def trace_full_adder(a: float, b: float, cin: float, mode: str = 'step'):
    """Trace full adder operation."""
    sum_out, carry_out = frozen_full_adder(a, b, cin)

    # Expected values
    total = int(a) + int(b) + int(cin)
    exp_sum = float(total & 1)
    exp_carry = float((total >> 1) & 1)

    frame = Frame("TRACE: FULL ADDER", width=52)

    frame.add_section("INPUT", [
        f"  a   = {Color.value(fmt_float(a))}  (bit)",
        f"  b   = {Color.value(fmt_float(b))}  (bit)",
        f"  cin = {Color.value(fmt_float(cin))}  (carry in)",
    ])

    frame.add_section("FORMULA", [
        f"  sum   = {Color.formula('XOR(XOR(a, b), cin)')}",
        f"  carry = {Color.formula('OR(AND(a,b), AND(XOR(a,b), cin))')}",
    ])

    # Intermediate values
    ab_xor = frozen_xor(a, b)
    ab_and = frozen_and(a, b)
    xor_cin_and = frozen_and(ab_xor, cin)

    frame.add_section("COMPUTE", [
        f"  step 1:  a XOR b      = {Color.value(fmt_float(ab_xor))}",
        f"  step 2:  XOR cin      = {Color.value(fmt_float(sum_out))}  (sum)",
        f"",
        f"  step 3:  a AND b      = {Color.value(fmt_float(ab_and))}",
        f"  step 4:  XOR AND cin  = {Color.value(fmt_float(xor_cin_and))}",
        f"  step 5:  OR           = {Color.value(fmt_float(carry_out))}  (carry)",
    ])

    frame.add_section("OUTPUT", [
        f"  sum   = {fmt_result(sum_out, exp_sum)}",
        f"  carry = {fmt_result(carry_out, exp_carry)}",
        f"",
        f"  {int(a)} + {int(b)} + {int(cin)} = {int(sum_out)} carry {int(carry_out)}",
    ])

    print()
    frame.display()
    print()


def trace_ripple(a: int, b: int, mode: str = 'step'):
    """Trace 8-bit ripple carry adder."""
    result, cout, carry_chain = frozen_ripple_add(a, b)
    expected = (a + b) & 0xFF
    exp_carry = 1 if (a + b) > 255 else 0

    frame = Frame("TRACE: 8-BIT RIPPLE ADDER", width=56)

    frame.add_section("INPUT", [
        f"  a = {Color.value(str(a)):>5}  {fmt_binary(a)}",
        f"  b = {Color.value(str(b)):>5}  {fmt_binary(b)}",
    ])

    frame.add_section("FORMULA", [
        f"  8 chained full adders, carry ripples through",
    ])

    # Build carry visualization
    carry_str = ''.join([Color.bit(c) for c in carry_chain])
    frame.add_section("CARRY CHAIN", [
        f"  bit:   7 6 5 4 3 2 1 0",
        f"  carry: {carry_str}",
    ])

    frame.add_section("OUTPUT", [
        f"  result = {Color.value(str(result)):>5}  {fmt_binary(result)}",
        f"  carry  = {Color.value(str(cout))}",
        f"",
        f"  {a} + {b} = {result}" + (f" (carry={cout})" if cout else "") +
        ("  " + Color.success("✓") if result == expected and cout == exp_carry else "  " + Color.error("✗")),
    ])

    print()
    frame.display()
    print()


def trace_alu(op: str, a: int, b: int, cin: int = 0, mode: str = 'step'):
    """Trace 6502 ALU operation."""
    result, cout, shape_name = alu_execute(op, a, b, cin)

    frame = Frame(f"TRACE: 6502 ALU - {op.upper()}", width=56)

    frame.add_section("INPUT", [
        f"  opcode  = {Color.value(op.upper())}",
        f"  A       = {Color.value(fmt_hex(a))}  ({a})",
        f"  operand = {Color.value(fmt_hex(b))}  ({b})" if op.upper() not in ['ASL', 'LSR', 'ROL', 'ROR', 'INC', 'DEC'] else f"  operand = (none)",
        f"  carry   = {Color.value(str(cin))}",
    ])

    frame.add_section("ROUTE", [
        f"  opcode {Color.dim('→')} shape: {Color.formula(shape_name)}",
    ])

    # Show operation-specific computation
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


# ============================================================================
# MAIN
# ============================================================================

def parse_value(s: str) -> int:
    """Parse value as int (supports hex with 0x prefix)."""
    s = s.strip()
    if s.startswith('0x') or s.startswith('0X'):
        return int(s, 16)
    return int(s)


def main():
    parser = argparse.ArgumentParser(
        description='Trace frozen shape operations step by step.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    python viz_trace.py xor 0 1
    python viz_trace.py and 1 1
    python viz_trace.py or 0 1
    python viz_trace.py not 1
    python viz_trace.py add 1 1 0        # full adder
    python viz_trace.py ripple 42 13     # 8-bit ripple add
    python viz_trace.py alu ADC 42 13    # 6502 ALU
    python viz_trace.py alu EOR 0x55 0xFF

"If you can show it, there's no magic."
"""
    )

    parser.add_argument('operation', help='Operation: xor, and, or, not, add, ripple, alu')
    parser.add_argument('args', nargs='*', help='Arguments for the operation')
    parser.add_argument('--auto', action='store_true', help='Auto-advance with animation')
    parser.add_argument('--instant', action='store_true', help='No pauses')
    parser.add_argument('--step', action='store_true', help='Step-by-step (default)')

    args = parser.parse_args()

    mode = 'instant' if args.instant else ('auto' if args.auto else 'step')
    op = args.operation.lower()
    op_args = args.args

    try:
        if op == 'xor':
            if len(op_args) != 2:
                print("Usage: viz_trace.py xor <a> <b>")
                sys.exit(1)
            a, b = float(op_args[0]), float(op_args[1])
            trace_xor(a, b, mode)

        elif op == 'and':
            if len(op_args) != 2:
                print("Usage: viz_trace.py and <a> <b>")
                sys.exit(1)
            a, b = float(op_args[0]), float(op_args[1])
            trace_and(a, b, mode)

        elif op == 'or':
            if len(op_args) != 2:
                print("Usage: viz_trace.py or <a> <b>")
                sys.exit(1)
            a, b = float(op_args[0]), float(op_args[1])
            trace_or(a, b, mode)

        elif op == 'not':
            if len(op_args) != 1:
                print("Usage: viz_trace.py not <a>")
                sys.exit(1)
            a = float(op_args[0])
            trace_not(a, mode)

        elif op == 'add':
            if len(op_args) < 2 or len(op_args) > 3:
                print("Usage: viz_trace.py add <a> <b> [cin]")
                sys.exit(1)
            a, b = float(op_args[0]), float(op_args[1])
            cin = float(op_args[2]) if len(op_args) > 2 else 0.0
            trace_full_adder(a, b, cin, mode)

        elif op == 'ripple':
            if len(op_args) != 2:
                print("Usage: viz_trace.py ripple <a> <b>")
                sys.exit(1)
            a, b = parse_value(op_args[0]), parse_value(op_args[1])
            trace_ripple(a, b, mode)

        elif op == 'alu':
            if len(op_args) < 2 or len(op_args) > 4:
                print("Usage: viz_trace.py alu <OP> <a> [b] [carry]")
                print("  OPs: ADC SBC AND ORA EOR ASL LSR ROL ROR INC DEC")
                sys.exit(1)
            alu_op = op_args[0].upper()
            a = parse_value(op_args[1])
            b = parse_value(op_args[2]) if len(op_args) > 2 else 0
            cin = int(op_args[3]) if len(op_args) > 3 else 0
            trace_alu(alu_op, a, b, cin, mode)

        else:
            print(f"Unknown operation: {op}")
            print("Supported: xor, and, or, not, add, ripple, alu")
            sys.exit(1)

    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
