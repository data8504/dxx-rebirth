"""Portable tests of selected-package identity; no macOS execution is simulated."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sdl2_compat',
    Path(__file__).resolve().parents[2] / 'common/arch/cocoa/sdl2_compat.py')
sdl2_compat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdl2_compat)


class TargetedTests(unittest.TestCase):
    command = ('/custom/bin/pkg-config', '--define-variable=prefix=/custom')

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.pcdir = self.root / 'selected/lib/pkgconfig'
        self.pcdir.mkdir(parents=True)
        self.libdir = self.pcdir.parent
        self.pc3dir = self.root / 'sdl3/lib/pkgconfig'
        self.pc3dir.mkdir(parents=True)
        self.lib3dir = self.pc3dir.parent
        self.sdl2 = self.library(self.libdir, 'libSDL2-2.0.0.dylib', 'libSDL2.dylib')
        self.sdl3 = self.library(self.lib3dir, 'libSDL3.0.dylib', 'libSDL3.dylib')
        (self.pc3dir / 'sdl3.pc').write_text('Name: sdl3\nVersion: 3.0.0\n')
        self.compat()
        self.values = {('sdl2', 'pcfiledir'): str(self.pcdir),
                       ('sdl2', 'libdir'): str(self.libdir),
                       ('sdl3', 'pcfiledir'): str(self.pc3dir),
                       ('sdl3', 'libdir'): str(self.lib3dir)}
        self.calls = []
        self.patcher = patch.object(sdl2_compat.subprocess, 'check_output',
                                    side_effect=self.output)
        self.mock_output = self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def library(self, directory, canonical, alias):
        path = directory / canonical
        path.write_bytes(b'library fixture')
        (directory / alias).symlink_to(canonical)
        return path

    def compat(self):
        # Deliberately not a version matching historical patch-level heuristics.
        (self.pcdir / 'sdl2-compat.pc').write_text(
            '# metadata\nName: sdl2_compat\nVersion: 2.0.1\nProvides: sdl2 = 2.0.1\n')
        (self.pcdir / 'sdl2.pc').symlink_to('sdl2-compat.pc')

    def output(self, args, *, text):
        self.assertTrue(text)
        self.assertEqual(tuple(args[:len(self.command)]), self.command)
        self.assertEqual(len(args), len(self.command) + 2)
        self.assertTrue(args[-2].startswith('--variable='))
        key = (args[-1], args[-2].split('=', 1)[1])
        self.calls.append(key)
        value = self.values[key]
        if isinstance(value, Exception):
            raise value
        return value + '\n'

    def test_selected_compat_discovers_sdl3_and_resolves_symlinks(self):
        library, inputs, selected = sdl2_compat.discover(self.command)
        self.assertEqual(library, self.sdl3)
        self.assertEqual(selected, self.sdl2)
        self.assertEqual(inputs, [self.pcdir / 'sdl2.pc', self.pc3dir / 'sdl3.pc'])
        self.assertEqual(self.calls, [('sdl2', 'pcfiledir'), ('sdl2', 'libdir'),
                                     ('sdl3', 'libdir'), ('sdl3', 'pcfiledir')])

    def test_selected_classic_ignores_installed_compat(self):
        # compat metadata remains installed; only the selected sdl2.pc changes.
        (self.pcdir / 'sdl2.pc').unlink()
        (self.pcdir / 'sdl2.pc').write_text('Name: sdl2\nVersion: 2.99.99\n')
        self.assertEqual(sdl2_compat.discover(self.command),
                         (None, [self.pcdir / 'sdl2.pc'], None))
        self.assertEqual(self.calls, [('sdl2', 'pcfiledir')])
        self.assertTrue((self.pcdir / 'sdl2-compat.pc').is_file())

    def test_explicit_override_works_without_pkgconfig(self):
        alias = self.lib3dir / 'libSDL3.dylib'
        self.assertEqual(sdl2_compat.discover((), str(alias)), (self.sdl3, [], None))
        self.mock_output.assert_not_called()

    def test_no_metadata_command_keeps_existing_path(self):
        self.assertEqual(sdl2_compat.discover(()), (None, [], None))
        self.mock_output.assert_not_called()

    def test_missing_sdl2_metadata_keeps_manual_build_path(self):
        self.values[('sdl2', 'pcfiledir')] = subprocess.CalledProcessError(1, 'pkg-config')
        self.assertEqual(sdl2_compat.discover(self.command), (None, [], None))

    def test_missing_sdl3_has_actionable_error(self):
        for failure in ('metadata', 'library'):
            with self.subTest(failure=failure):
                if failure == 'metadata':
                    self.values[('sdl3', 'libdir')] = subprocess.CalledProcessError(1, 'pkg-config')
                else:
                    self.values[('sdl3', 'libdir')] = str(self.root / 'missing')
                with self.assertRaisesRegex(ValueError, 'macos_sdl3_library='):
                    sdl2_compat.discover(self.command)

    def test_verify_selected_matches_actual_linked_alias(self):
        linked = self.libdir / 'libSDL2.dylib'
        listing = f'/tmp/game:\n\t{linked} (compatibility version 1.0.0)\n'
        self.mock_output.side_effect = None
        self.mock_output.return_value = listing
        sdl2_compat.verify_selected('/tmp/game', self.sdl2)
        self.mock_output.assert_called_once_with(['otool', '-L', '/tmp/game'], text=True)

    def test_verify_selected_rejects_other_linked_sdl2(self):
        listing = f'/tmp/game:\n\t{self.sdl3} (compatibility version 1.0.0)\n'
        self.mock_output.side_effect = None
        self.mock_output.return_value = listing
        with self.assertRaisesRegex(ValueError, 'does not match.*consistent SDL2'):
            sdl2_compat.verify_selected('/tmp/game', self.sdl2)

    def test_no_selected_identity_requires_no_linkage_query(self):
        sdl2_compat.verify_selected('/tmp/game', None)
        self.mock_output.assert_not_called()


if __name__ == '__main__':
    unittest.main()
