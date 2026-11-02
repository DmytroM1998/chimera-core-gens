import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
ORIGINAL = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "io.asm"
PORT = ROOT / "waterbox" / "gens_io_data_x64.asm"


def original_data(text):
    source = text.split("section .data", 1)[1].split("\n\tTable_TP1:", 1)[0]
    matches = re.findall(
        r"^\s*DECL\s+(Controller_\w+|Lag_Frame)\s*\n\s*(dd|db)\s+(\S+)",
        source, re.MULTILINE,
    )
    return [(name, 4 if kind == "dd" else 1, int(value, 0)) for name, kind, value in matches]


def expanded_data(text):
    macros = {}
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        match = re.fullmatch(r"%macro\s+(\w+)\s+(\d+)", lines[index])
        if match:
            name, count = match.groups()
            body = []
            index += 1
            while index < len(lines) and lines[index] != "%endmacro":
                body.append(lines[index])
                index += 1
            if index == len(lines):
                raise AssertionError("unterminated macro")
            macros[name] = (int(count), body)
        index += 1

    def expand(line):
        command, _, arguments = line.strip().partition(" ")
        if command not in macros:
            return [line.strip()]
        count, body = macros[command]
        args = [argument.strip() for argument in arguments.split(",")]
        if len(args) != count:
            raise AssertionError("macro argument count")
        result = []
        for item in body:
            item = re.sub(r"%([1-9]\d*)", lambda m: args[int(m.group(1)) - 1], item)
            result.extend(expand(item))
        return result

    data = text.split("section .data", 1)[1].split("section .note.GNU-stack", 1)[0]
    expanded = [out for line in data.splitlines() for out in expand(line)]
    globals_ = {line[7:] for line in expanded if line.startswith("global ")}
    labels = {line[:-1] for line in expanded if line.endswith(":")}
    entries = []
    for line in expanded:
        match = re.fullmatch(r"(Controller_\w+|Lag_Frame):\s*(dd|db)\s+(\S+)", line)
        if match:
            name, kind, value = match.groups()
            if name not in globals_ or "_" + name not in globals_ or "_" + name not in labels:
                raise AssertionError("missing public aliases for " + name)
            entries.append((name, 4 if kind == "dd" else 1, int(value, 0)))
    return entries


class ControllerLayoutTests(unittest.TestCase):
    def test_exact_pinned_global_order_and_initial_values(self):
        original = original_data(ORIGINAL.read_text(encoding="latin-1"))
        actual = expanded_data(PORT.read_text(encoding="utf-8"))
        self.assertEqual(len(original), 113)
        self.assertEqual(original, actual)
        self.assertEqual(sum(size for _, size, _ in actual[:-1]), 448)
        self.assertEqual(actual[-1], ("Lag_Frame", 1, 0))

    def test_negative_controls(self):
        original = original_data(ORIGINAL.read_text(encoding="latin-1"))
        text = PORT.read_text(encoding="utf-8")
        for old, new in (
            ("VARIABLE Controller_%1_Z, 1", "VARIABLE Controller_%1_Z, 0"),
            ("PORT 2\nPAD 1B", "PAD 1B\nPORT 2"),
        ):
            with self.subTest(mutation=old):
                self.assertIn(old, text)
                self.assertNotEqual(original, expanded_data(text.replace(old, new)))


if __name__ == "__main__":
    unittest.main()
