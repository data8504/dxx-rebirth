"""Portable closure tests; mocked otool output does not validate Mac execution."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('homebrew_bundle',
    Path(__file__).resolve().parents[2] / 'common/arch/cocoa/homebrew_bundle.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cellar = self.root / 'brew/Cellar'
        self.cellar.mkdir(parents=True)
        self.formulae, self.links = [], {}
        self.exe = self.file(self.root / 'game')
        self.addCleanup(patch.stopall)
        patch.object(bundle, 'output', side_effect=self.output).start()

    def file(self, path, dependencies=()):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'fixture')
        self.links[path.resolve()] = dependencies
        return path

    def keg(self, name, version='1', dependencies=()):
        formula = next((f for f in self.formulae if f['name'] == name), None)
        if formula is None:
            formula = dict(name=name, installed=[], build_dependencies=['build-only'],
                           test_dependencies=['test-only'])
            self.formulae.append(formula)
        formula['installed'].append(dict(version=version,
            runtime_dependencies=[dict(full_name=n, pkg_version=v) for n, v in dependencies]))
        return self.cellar / name / version

    def output(self, *args):
        if args == ('brew', '--cellar'):
            return str(self.cellar)
        if args == ('brew', 'info', '--json=v2', '--installed', '--formula'):
            return json.dumps(dict(formulae=self.formulae))
        if args[:2] == ('otool', '-L'):
            return str(args[2]) + ':\n' + '\n'.join(
                '\t' + str(p) + ' (compatibility version 1.0.0, current version 1.0.0)'
                for p in self.links[Path(args[2]).resolve()])
        self.fail(f'unexpected command: {args}')

    def test_metadata_and_linkage_reach_recursive_fixed_point(self):
        a = self.file(self.keg('a', dependencies=[('b', '1')]) / 'lib/liba.dylib')
        c = self.file(self.keg('c', dependencies=[('d', '1')]) / 'lib/libc.dylib')
        d = self.file(self.keg('d', dependencies=[('a', '1')]) / 'lib/libd.dylib')
        external = self.file(self.root / 'vendor/libexternal.dylib', [c])
        b = self.file(self.keg('b') / 'lib/libb.dylib', [external])
        self.links[self.exe] = [a, '/usr/lib/libSystem.B.dylib']
        names, packages, substitutions = bundle.discover(self.exe)
        self.assertEqual(set(names.values()), {a, b, c, d, external})
        self.assertEqual(packages, [(n, '1') for n in 'abcd'])
        self.assertEqual(substitutions, [])

    def test_alias_ownership_and_canonical_deduplication(self):
        real = self.file(self.keg('canonical', '2') / 'lib/liba.2.dylib')
        alias = real.parent / 'liba.dylib'
        alias.symlink_to(real.name)
        opt = self.cellar.parent / 'opt/alias'
        opt.parent.mkdir()
        opt.symlink_to(real.parents[1])
        self.links[self.exe] = [opt / 'lib/liba.dylib', real]
        names, packages, _ = bundle.discover(self.exe)
        self.assertEqual(names, {'liba.2.dylib': real, 'liba.dylib': real})
        self.assertEqual(packages, [('canonical', '2')])

    def test_exact_version_precedes_active_substitution(self):
        consumer = self.file(self.keg('a', dependencies=[('b', '1')]) / 'lib/liba.dylib')
        old = self.file(self.keg('b', '1') / 'lib/libb.dylib')
        current = self.file(self.keg('b', '2') / 'lib/libb.dylib')
        opt = self.cellar.parent / 'opt/b'
        opt.parent.mkdir()
        opt.symlink_to(current.parents[1])
        self.links[self.exe] = [consumer]
        names, packages, substitutions = bundle.discover(self.exe)
        self.assertIn(old, names.values())
        self.assertNotIn(current, names.values())
        self.assertEqual(substitutions, [])
        self.formulae[1]['installed'].pop(0)
        names, packages, substitutions = bundle.discover(self.exe)
        self.assertIn(current, names.values())
        self.assertEqual(substitutions, [[['a', '1'], ['b', '1'], ['b', '2']]])

    def test_collision_fails_before_copying(self):
        first = self.file(self.root / 'one/libsame.dylib')
        second = self.file(self.root / 'two/libsame.dylib')
        self.links[self.exe] = [first, second]
        with self.assertRaisesRegex(ValueError, 'filename collision'):
            bundle.discover(self.exe)

    def test_nonbrew_absolute_linkage_is_preserved(self):
        second = self.file(self.root / 'vendor/libsecond.dylib')
        first = self.file(self.root / 'vendor/libfirst.dylib', [second])
        self.links[self.exe] = [first]
        names, packages, _ = bundle.discover(self.exe)
        self.assertEqual(set(names.values()), {first, second})
        self.assertEqual(packages, [])

    def test_missing_installed_metadata_is_diagnosed(self):
        library = self.file(self.keg('a') / 'lib/liba.dylib')
        self.links[self.exe] = [library]
        self.formulae[0]['installed'][0]['runtime_dependencies'] = None
        with self.assertRaisesRegex(ValueError, 'missing installed runtime dependencies'):
            bundle.discover(self.exe)
        self.formulae.clear()
        with self.assertRaisesRegex(ValueError, 'no installed receipt metadata'):
            bundle.discover(self.exe)

    def test_relative_rpath_is_rejected(self):
        self.links[self.exe] = ['@rpath/libunknown.dylib']
        with self.assertRaisesRegex(ValueError, 'absolute-path discovery required'):
            bundle.discover(self.exe)

    def test_helpers_plugins_data_are_outside_selection(self):
        keg = self.keg('a')
        library = self.file(keg / 'lib/liba.dylib')
        for relative in ('lib/plugins/plugin.dylib', 'libexec/helper', 'share/runtime.dat'):
            self.file(keg / relative)
        self.links[self.exe] = [library]
        names, _, _ = bundle.discover(self.exe)
        self.assertEqual(set(names.values()), {library})


if __name__ == '__main__':
    unittest.main()
