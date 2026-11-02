from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from extract_audio_functions import extract_function


class ExtractAudioFunctionsTests(unittest.TestCase):
    def test_static_int_function_with_arguments(self):
        source = "static int lowpass (int* buf, int i, int len) { return buf[i]; }"
        self.assertEqual(extract_function(source, "lowpass"), source)
        source = "static int lowpass(int* buf) // comment }\n{ return buf[0]; }"
        self.assertEqual(extract_function(source, "lowpass"), source)
        with self.assertRaisesRegex(ValueError, "exactly one"):
            extract_function("void update(int length);", "update")

    def test_braces_in_comments_and_strings(self):
        source = ('void PSG_Special_Update(void) { /* } */ '
                  'const char *s = "{\\\"}"; // }\n'
                  "char c = '{'; { } }")
        self.assertEqual(extract_function(source, "PSG_Special_Update"), source)

    def test_missing_duplicate_and_unterminated(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            extract_function("", "PSG_Special_Update")
        source = "void PSG_Special_Update(void) {}"
        with self.assertRaisesRegex(ValueError, "exactly one"):
            extract_function(source + "\n" + source, "PSG_Special_Update")
        with self.assertRaisesRegex(ValueError, "unterminated"):
            extract_function("void PSG_Special_Update(void) {", "PSG_Special_Update")


if __name__ == "__main__":
    unittest.main()
