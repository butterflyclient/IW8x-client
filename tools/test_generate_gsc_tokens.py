import unittest

from generate_gsc_tokens import OUTPUT, SOURCE, generate


class ScriptTokens(unittest.TestCase):
    def test_canonical_ids_spelling_order_and_comments(self):
        source = b'''// { 0xFFFF, "commented_out" },
            { 0x002C, "aiSpread" },
            { 0x1012, "aispread" },
            { 0x24C5, "buildweapon" },
            { 0x1184A, "wave_enemies_remaining" },
            { 0x9999, "buildweapon" },
            { 0x0012, "scripts/code/struct" },
            { 0x0000, "" },
        '''
        generated = generate(source)
        self.assertIn('{"buildweapon", 0x24C5}', generated)
        self.assertIn('{"wave_enemies_remaining", 0x1184A}', generated)
        self.assertLess(generated.index('{"aiSpread"'), generated.index('{"aispread"'))
        self.assertNotIn("0x9999", generated)
        self.assertNotIn("commented_out", generated)
        self.assertIn('{"scripts/code/struct", 0x12}', generated)

    def test_mw144_isnightmap_module_and_export(self):
        source = b'''{ 0x08C4, "scripts/cp_mp/utility/game_utility" },
            { 0x72B6, "isnightmap" },
            { 0x0723, "scripts/mp/class" },
        '''
        generated = generate(source)
        self.assertIn('{"scripts/cp_mp/utility/game_utility", 0x8C4}', generated)
        self.assertIn('{"isnightmap", 0x72B6}', generated)
        self.assertIn('{"scripts/mp/class", 0x723}', generated)

    def test_empty_source_rejected(self):
        with self.assertRaises(ValueError):
            generate(b"// no token entries")

    @unittest.skipUnless(SOURCE.exists(), "optional xensik source checkout absent")
    def test_checked_in_table_matches_reference(self):
        self.assertEqual(OUTPUT.read_text(encoding="utf-8"), generate(SOURCE.read_bytes()))


if __name__ == "__main__":
    unittest.main()
