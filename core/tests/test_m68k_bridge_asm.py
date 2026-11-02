import random
import re
import unittest
from pathlib import Path


SOURCE = Path(__file__).resolve().parent.parent / "waterbox" / "gens_m68k_bridge_x64.asm"
MASK64 = (1 << 64) - 1
ALIASES = {
    "eax": "rax", "ecx": "rcx", "edx": "rdx", "edi": "rdi",
    "r8d": "r8", "r10d": "r10", "r12d": "r12",
}


def expand(text, name):
    definitions = {
        key: int(value)
        for key, value in re.findall(r"^%define\s+(\w+)\s+(\d+)\s*$", text, re.MULTILINE)
    }
    invocations = re.findall(r"^BRIDGE\s+(\w+),\s*(\d+),\s*(\d+),\s*(\d+)\s*$",
                             text, re.MULTILINE)
    matches = [item for item in invocations if item[0] == name]
    if len(matches) != 1:
        raise AssertionError("bridge entry must be unique")
    body = text.split("%macro BRIDGE 4\n", 1)[1].split("%endmacro", 1)[0]
    for index, value in enumerate(matches[0], 1):
        body = body.replace("%" + str(index), value)
    writing = int(matches[0][3])
    enabled = True
    instructions = []
    labels = {}
    for raw in body.splitlines():
        line = raw.strip()
        if line.startswith("%if "):
            if line != "%if " + str(writing) + " = 0":
                raise AssertionError("unexpected conditional")
            enabled = not writing
        elif line == "%else":
            enabled = not enabled
        elif line == "%endif":
            enabled = True
        elif enabled and line and not line.startswith("global "):
            if line.endswith(":"):
                labels[line[:-1]] = len(instructions)
            else:
                instructions.append(line)
    return definitions, matches[0], instructions, labels


class Machine:
    def __init__(self, definitions, invocation, instructions, labels, seed, status, skew):
        rng = random.Random(seed)
        self.reg = {name: rng.getrandbits(64) for name in (
            "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15")}
        self.reg.update(rsp=0x100000000 + skew, r12=0x36f12345678,
                        rsi=0x36fab001236, rbp=0x36f00002000)
        self.initial = self.reg.copy()
        self.mem = {}
        self.definitions = definitions
        self.invocation = invocation
        self.instructions = instructions
        self.labels = labels
        self.status = status
        self.zero = False
        self.carry = False
        self.calls = 0

    def address(self, expression):
        match = re.fullmatch(r"\[rsp(?:\s*\+\s*(\w+))?\]", expression)
        if not match:
            raise AssertionError("unsupported address " + expression)
        return (self.reg["rsp"] + self.definitions.get(match.group(1), 0)) & MASK64

    def read(self, operand, width):
        operand = operand.removeprefix("dword ")
        if operand.startswith("["):
            address = self.address(operand)
            return sum(self.mem.get(address + n, 0) << (8 * n) for n in range(width // 8))
        if operand in ALIASES:
            return self.reg[ALIASES[operand]] & 0xffffffff
        if operand in self.reg:
            return self.reg[operand]
        if operand in self.definitions:
            return self.definitions[operand]
        if operand == "gens_m68k_read":
            return 0x36f10000000
        if operand == "gens_m68k_write":
            return 0x36f20000000
        return int(operand, 0) & MASK64

    def write(self, operand, value, width):
        operand = operand.removeprefix("dword ")
        if operand.startswith("["):
            address = self.address(operand)
            for n in range(width // 8):
                self.mem[address + n] = (value >> (8 * n)) & 0xff
        elif operand in ALIASES:
            self.reg[ALIASES[operand]] = value & 0xffffffff
        else:
            self.reg[operand] = value & MASK64

    def width(self, target, source):
        if target.startswith("dword ") or source.startswith("dword "):
            return 32
        for operand in (target, source):
            if operand in ALIASES:
                return 32
            if operand in self.reg:
                return 64
        raise AssertionError("ambiguous operand width")

    def helper(self):
        name, size, order, writing = self.invocation
        self.calls += 1
        assert self.reg["rsp"] % 16 == 0
        assert self.reg["rax"] == (0x36f20000000 if int(writing) else 0x36f10000000)
        assert self.reg["rdi"] == self.initial["r12"]
        assert self.reg["rsi"] == self.reg["rsp"]
        assert self.reg["rdx"] == int(size)
        assert self.reg["rcx"] == int(order)
        if int(writing):
            assert self.reg["r8"] == self.initial["rcx"] & 0xffffffff
        else:
            assert self.reg["r8"] == self.reg["rsp"] + self.definitions["RESULT"]
        assert self.read("[rsp + LIVE_PC]", 64) == self.initial["rsi"]
        assert self.read("[rsp + LIVE_BASE]", 64) == self.initial["rbp"]
        assert self.read("[rsp + LIVE_CYCLES]", 32) == self.initial["rdi"] & 0xffffffff
        assert self.read("[rsp + LIVE_ADDRESS]", 32) == self.initial["rdx"] & 0xffffffff
        self.write("[rsp + LIVE_PC]", self.initial["rsi"] + 0x100000000, 64)
        self.write("[rsp + LIVE_BASE]", self.initial["rbp"] + 0x100000020, 64)
        self.write("[rsp + LIVE_CYCLES]", (self.initial["rdi"] + 7) & 0xffffffff, 32)
        self.write("[rsp + LIVE_ADDRESS]", 0xfe123456, 32)
        if not int(writing) and not self.status:
            self.write("[rsp + RESULT]", 0xfedcba98, 32)
        for register in ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"):
            self.reg[register] = 0xdeadbeefdeadbeef
        self.reg["rax"] = self.status

    def run(self):
        pc = 0
        while pc < len(self.instructions):
            line = self.instructions[pc]
            pc += 1
            operation, _, operands = line.partition(" ")
            args = [part.strip() for part in operands.split(",")] if operands else []
            if operation == "mov":
                width = self.width(*args)
                self.write(args[0], self.read(args[1], width), width)
            elif operation == "lea":
                self.write(args[0], self.address(args[1]), 64)
            elif operation == "push":
                self.reg["rsp"] -= 8
                self.write("[rsp]", self.read(args[0], 64), 64)
            elif operation == "pop":
                self.write(args[0], self.read("[rsp]", 64), 64)
                self.reg["rsp"] += 8
            elif operation in ("and", "sub"):
                left, right = self.read(args[0], 64), self.read(args[1], 64)
                self.write(args[0], (left & right) if operation == "and" else (left - right), 64)
            elif operation == "call":
                assert args == ["rax"]
                self.helper()
            elif operation in ("cmp", "test"):
                width = self.width(*args)
                left, right = self.read(args[0], width), self.read(args[1], width)
                self.zero = left == right if operation == "cmp" else (left & right) == 0
            elif operation in ("jne", "jz"):
                if self.zero == (operation == "jz"):
                    pc = self.labels[args[0]]
            elif operation in ("stc", "clc"):
                self.carry = operation == "stc"
            elif operation == "ret":
                break
            else:
                raise AssertionError("unsupported instruction " + line)
        else:
            raise AssertionError("bridge did not return")
        assert self.calls == 1
        for register in ("rax", "rbx", "r8", "r9", "r11", "r12", "r13", "r14", "r15", "rsp"):
            assert self.reg[register] == self.initial[register], register
        assert self.reg["rsi"] == self.initial["rsi"] + 0x100000000
        assert self.reg["rbp"] == self.initial["rbp"] + 0x100000020
        assert self.reg["rdi"] == (self.initial["rdi"] + 7) & 0xffffffff
        assert self.reg["rdx"] == 0xfe123456
        assert self.reg["r10"] == self.status
        assert self.carry == bool(self.status)
        expected = 0xfedcba98 if not int(self.invocation[3]) and not self.status else self.initial["rcx"]
        assert self.reg["rcx"] == expected


class BridgeAssemblyModelTests(unittest.TestCase):
    def test_all_entries_with_both_stack_alignments_and_failure(self):
        text = SOURCE.read_text(encoding="utf-8")
        names = re.findall(r"^BRIDGE\s+(\w+),", text, re.MULTILINE)
        self.assertEqual(len(names), 12)
        self.assertEqual(len(set(names)), 12)
        for name in names:
            expanded = expand(text, name)
            for skew in (0, 8):
                for status in (0, 2, 4):
                    with self.subTest(entry=name, skew=skew, status=status):
                        Machine(*expanded, seed=123, status=status, skew=skew).run()

    def test_negative_controls(self):
        text = SOURCE.read_text(encoding="utf-8")
        mutations = (
            ("mov rdi, r12", "mov edi, r12d", "read_long", 0),
            ("and rsp, -16", "and rsp, -8", "read_long", 0),
            ("mov r8d, [rsp + SAVE_RCX]", "mov r8d, [rsp + LIVE_ADDRESS]", "write_long", 0),
            ("mov rax, [rsp + SAVE_RAX]", "mov rax, [rsp + SAVE_RBX]", "read_long", 0),
        )
        for old, new, name, skew in mutations:
            with self.subTest(mutation=old):
                self.assertIn(old, text)
                with self.assertRaises(AssertionError):
                    Machine(*expand(text.replace(old, new), name), seed=123, status=0, skew=skew).run()


if __name__ == "__main__":
    unittest.main()
