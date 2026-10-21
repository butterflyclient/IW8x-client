import json
from pathlib import Path
import tempfile
import unittest
from compile_plugin_gsc import expand, load_bindings


class BindingTests(unittest.TestCase):
    def test_methods_and_functions(self):
        self.assertEqual(expand('x = native_add(1, 2); self native_test();',
                                {'native_add': '_func_0438', 'native_test': '_meth_87CA'}),
                         'x = _func_0438(1, 2); self _meth_87CA();')

    def test_literals_and_comments(self):
        source = '// native_add\n/* native_add */\nx = "native_add \\""; native_add();'
        self.assertEqual(expand(source, {'native_add': '_func_0438'}), source.replace('native_add();', '_func_0438();'))

    def test_word_boundaries_and_case(self):
        self.assertEqual(expand('native_add_extra NATIVE_ADD', {'native_add': 'alias'}), 'native_add_extra alias')

    def test_directives_and_lines(self):
        source = '#inline iw8x_native_bindings;\n#include maps/native_add;\nnative_add();\n'
        actual = expand(source, {'native_add': 'alias'})
        self.assertEqual(actual.count('\n'), source.count('\n'))
        self.assertIn('#include maps/native_add;', actual)
        self.assertTrue(actual.endswith('alias();\n'))
        self.assertNotIn('#inline', actual)

    def test_database(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bindings.json'
            item = {'name': 'native_add', 'method': False, 'slot': 0, 'plugin': 'Example'}
            path.write_text(json.dumps({'api': 1, 'bindings': [item]}))
            self.assertEqual(load_bindings(path), {'native_add': '_func_0438'})
            path.write_text(json.dumps({'api': 1, 'bindings': [item, item]}))
            with self.assertRaises(ValueError): load_bindings(path)
            item['slot'] = 256
            path.write_text(json.dumps({'api': 1, 'bindings': [item]}))
            with self.assertRaises(ValueError): load_bindings(path)


if __name__ == '__main__': unittest.main()
