"""
TriX Visualization Core

Terminal rendering primitives for frozen shape visualization.
Monochrome-first, color-enhanced, 80-column friendly.

"If you can show it, there's no magic."
"""

import os
import sys
import time
from typing import List, Optional, Tuple

# ============================================================================
# COLOR SUPPORT
# ============================================================================

def supports_color() -> bool:
    """Check if terminal supports ANSI colors."""
    if os.environ.get('NO_COLOR'):
        return False
    if not hasattr(sys.stdout, 'isatty'):
        return False
    if not sys.stdout.isatty():
        return False
    return True

USE_COLOR = supports_color()

class Color:
    """ANSI color codes with graceful degradation."""

    RESET = '\033[0m' if USE_COLOR else ''
    BOLD = '\033[1m' if USE_COLOR else ''
    DIM = '\033[2m' if USE_COLOR else ''

    # Semantic colors
    GREEN = '\033[32m' if USE_COLOR else ''      # positive, true, 1
    RED = '\033[31m' if USE_COLOR else ''        # negative, false, 0
    YELLOW = '\033[33m' if USE_COLOR else ''     # carry, attention
    BLUE = '\033[34m' if USE_COLOR else ''       # info, labels
    CYAN = '\033[36m' if USE_COLOR else ''       # values
    MAGENTA = '\033[35m' if USE_COLOR else ''    # formulas
    WHITE = '\033[37m' if USE_COLOR else ''      # normal

    @classmethod
    def success(cls, text: str) -> str:
        return f"{cls.GREEN}{text}{cls.RESET}"

    @classmethod
    def error(cls, text: str) -> str:
        return f"{cls.RED}{text}{cls.RESET}"

    @classmethod
    def warn(cls, text: str) -> str:
        return f"{cls.YELLOW}{text}{cls.RESET}"

    @classmethod
    def info(cls, text: str) -> str:
        return f"{cls.BLUE}{text}{cls.RESET}"

    @classmethod
    def value(cls, text: str) -> str:
        return f"{cls.CYAN}{text}{cls.RESET}"

    @classmethod
    def formula(cls, text: str) -> str:
        return f"{cls.MAGENTA}{text}{cls.RESET}"

    @classmethod
    def bold(cls, text: str) -> str:
        return f"{cls.BOLD}{text}{cls.RESET}"

    @classmethod
    def dim(cls, text: str) -> str:
        return f"{cls.DIM}{text}{cls.RESET}"

    @classmethod
    def bit(cls, val: float) -> str:
        """Color a bit value: 1=green, 0=dim."""
        if val > 0.5:
            return f"{cls.GREEN}1{cls.RESET}"
        else:
            return f"{cls.DIM}0{cls.RESET}"


# ============================================================================
# BOX DRAWING
# ============================================================================

class Box:
    """Unicode box drawing."""

    # Single line
    H = '─'    # horizontal
    V = '│'    # vertical
    TL = '┌'   # top-left
    TR = '┐'   # top-right
    BL = '└'   # bottom-left
    BR = '┘'   # bottom-right
    LT = '├'   # left-tee
    RT = '┤'   # right-tee
    TT = '┬'   # top-tee
    BT = '┴'   # bottom-tee
    X = '┼'    # cross

    # Double line (for outer box)
    DH = '═'
    DV = '║'
    DTL = '╔'
    DTR = '╗'
    DBL = '╚'
    DBR = '╝'
    DLT = '╠'
    DRT = '╣'

    @classmethod
    def single(cls, width: int, title: str = '', content: List[str] = None) -> List[str]:
        """Draw a single-line box."""
        content = content or []
        inner_width = width - 2

        lines = []

        # Top border with optional title
        if title:
            title_part = f" {title} "
            remaining = inner_width - len(title_part)
            top = cls.TL + cls.H + title_part + cls.H * remaining + cls.TR
        else:
            top = cls.TL + cls.H * inner_width + cls.TR
        lines.append(top)

        # Content lines
        for line in content:
            # Strip ANSI codes for length calculation
            visible_len = len(strip_ansi(line))
            padding = inner_width - visible_len
            lines.append(cls.V + line + ' ' * padding + cls.V)

        # Bottom border
        lines.append(cls.BL + cls.H * inner_width + cls.BR)

        return lines

    @classmethod
    def double(cls, width: int, title: str = '', content: List[str] = None) -> List[str]:
        """Draw a double-line box."""
        content = content or []
        inner_width = width - 2

        lines = []

        # Top border with optional title
        if title:
            title_part = f" {title} "
            remaining = inner_width - len(title_part)
            top = cls.DTL + cls.DH + title_part + cls.DH * remaining + cls.DTR
        else:
            top = cls.DTL + cls.DH * inner_width + cls.DTR
        lines.append(top)

        # Content lines
        for line in content:
            visible_len = len(strip_ansi(line))
            padding = inner_width - visible_len
            lines.append(cls.DV + line + ' ' * padding + cls.DV)

        # Bottom border
        lines.append(cls.DBL + cls.DH * inner_width + cls.DBR)

        return lines


# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

def strip_ansi(text: str) -> str:
    """Remove ANSI escape codes from text."""
    import re
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)


def center(text: str, width: int) -> str:
    """Center text, accounting for ANSI codes."""
    visible_len = len(strip_ansi(text))
    padding = width - visible_len
    left_pad = padding // 2
    right_pad = padding - left_pad
    return ' ' * left_pad + text + ' ' * right_pad


def ljust(text: str, width: int) -> str:
    """Left-justify text, accounting for ANSI codes."""
    visible_len = len(strip_ansi(text))
    padding = width - visible_len
    return text + ' ' * max(0, padding)


def rjust(text: str, width: int) -> str:
    """Right-justify text, accounting for ANSI codes."""
    visible_len = len(strip_ansi(text))
    padding = width - visible_len
    return ' ' * max(0, padding) + text


# ============================================================================
# DISPLAY FUNCTIONS
# ============================================================================

def clear_screen():
    """Clear terminal screen."""
    print('\033[2J\033[H', end='')


def print_lines(lines: List[str], indent: int = 0):
    """Print lines with optional indent."""
    prefix = ' ' * indent
    for line in lines:
        print(prefix + line)


def pause(message: str = "Press Enter to continue..."):
    """Wait for user input."""
    try:
        input(Color.dim(message))
    except EOFError:
        pass


def animate_delay(seconds: float = 0.3):
    """Delay for animation."""
    time.sleep(seconds)


# ============================================================================
# FRAME SYSTEM
# ============================================================================

class Frame:
    """A single visualization frame."""

    def __init__(self, title: str, width: int = 44):
        self.title = title
        self.width = width
        self.sections: List[Tuple[str, List[str]]] = []

    def add_section(self, name: str, lines: List[str]):
        """Add a named section to the frame."""
        self.sections.append((name, lines))

    def render(self) -> List[str]:
        """Render the frame as lines."""
        all_content = []

        for section_name, section_lines in self.sections:
            # Section header
            inner_box = Box.single(self.width - 4, section_name, section_lines)
            for line in inner_box:
                all_content.append(' ' + line + ' ')
            all_content.append('')  # spacing

        # Remove trailing empty line
        if all_content and all_content[-1] == '':
            all_content = all_content[:-1]

        return Box.double(self.width, self.title, all_content)

    def display(self, indent: int = 0):
        """Display the frame."""
        print_lines(self.render(), indent)


# ============================================================================
# VALUE FORMATTING
# ============================================================================

def fmt_float(val: float, precision: int = 1) -> str:
    """Format float for display."""
    return f"{val:.{precision}f}"


def fmt_binary(val: int, bits: int = 8) -> str:
    """Format integer as binary with bit coloring."""
    result = []
    for i in range(bits - 1, -1, -1):
        bit = (val >> i) & 1
        result.append(Color.bit(bit))
    return ''.join(result)


def fmt_hex(val: int) -> str:
    """Format integer as hex."""
    return f"0x{val:02X}"


def fmt_result(val: float, expected: float = None) -> str:
    """Format result with optional check mark."""
    result = fmt_float(val)
    if expected is not None:
        if abs(val - expected) < 0.001:
            return Color.success(result + "  ✓")
        else:
            return Color.error(result + "  ✗")
    return Color.value(result)


# ============================================================================
# BITS VISUALIZATION
# ============================================================================

def render_bits(val: int, bits: int = 8, label: str = "") -> str:
    """Render an integer as colored bits with label."""
    bit_str = fmt_binary(val, bits)
    if label:
        return f"  {label}: {bit_str}  ({val})"
    return f"  {bit_str}  ({val})"


def render_bit_operation(a: int, b: int, result: int, op: str, bits: int = 8) -> List[str]:
    """Render a bitwise operation."""
    return [
        f"  a:      {fmt_binary(a, bits)}  ({a})",
        f"  b:      {fmt_binary(b, bits)}  ({b})",
        f"  {' ' * len(op)}      " + "─" * (bits + 4),
        f"  {op}:   {fmt_binary(result, bits)}  ({result})",
    ]


# ============================================================================
# STEP-BY-STEP COMPUTATION
# ============================================================================

class ComputeStep:
    """A single step in a computation."""

    def __init__(self, description: str, expression: str, result: str):
        self.description = description
        self.expression = expression
        self.result = result

    def render(self, step_num: int, width: int = 36) -> List[str]:
        """Render this step."""
        return [
            f"  step {step_num}:  {Color.dim(self.description)}",
            f"           {Color.formula(self.expression)}",
            f"           = {Color.value(self.result)}",
        ]


def render_computation(steps: List[ComputeStep], width: int = 36) -> List[str]:
    """Render a sequence of computation steps."""
    lines = []
    for i, step in enumerate(steps, 1):
        lines.extend(step.render(i, width))
        if i < len(steps):
            lines.append('')  # spacing between steps
    return lines


# ============================================================================
# MAIN TEST
# ============================================================================

if __name__ == '__main__':
    print("\nTriX Visualization Core - Test\n")

    # Test box drawing
    content = [
        f"  a = {Color.value('0.0')}",
        f"  b = {Color.value('1.0')}",
    ]
    box = Box.single(40, "INPUT", content)
    print_lines(box)
    print()

    # Test frame
    frame = Frame("TRACE: XOR", width=48)
    frame.add_section("INPUT", [
        f"  a = {Color.value('0.0')}",
        f"  b = {Color.value('1.0')}",
    ])
    frame.add_section("FORMULA", [
        f"  XOR(a, b) = {Color.formula('a + b - 2·a·b')}",
    ])
    frame.add_section("OUTPUT", [
        f"  result = {fmt_result(1.0, 1.0)}",
    ])
    frame.display()
    print()

    # Test bits
    print("Bit visualization:")
    print(render_bits(42, label="A"))
    print(render_bits(13, label="B"))
    print()

    # Test computation steps
    steps = [
        ComputeStep("add a + b", "0.0 + 1.0", "1.0"),
        ComputeStep("multiply 2·a·b", "2 × 0.0 × 1.0", "0.0"),
        ComputeStep("subtract", "1.0 - 0.0", "1.0"),
    ]
    print("Computation steps:")
    for line in render_computation(steps):
        print(line)

    print(f"\n{Color.success('Core visualization test complete.')}\n")
