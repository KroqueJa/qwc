"""Oracle discovery must not silently substitute uutils/BSD for GNU wc."""
import os
import subprocess
import unittest
from unittest.mock import patch

from session import find_gnu_wc


class OracleTests(unittest.TestCase):
    def setUp(self):
        find_gnu_wc.cache_clear()
        self.addCleanup(find_gnu_wc.cache_clear)

    @patch.dict(os.environ, {}, clear=True)
    def test_prefers_renamed_gnu_binary(self):
        with patch('session.shutil.which', side_effect=lambda name: '/usr/bin/' + name), \
             patch('session.subprocess.run', return_value=subprocess.CompletedProcess(
                 [], 0, b'wc (GNU coreutils) 9.10\n', b'')) as run:
            self.assertEqual(find_gnu_wc(), '/usr/bin/gnuwc')
            self.assertEqual(run.call_args.args[0], ['/usr/bin/gnuwc', '--version'])

    @patch.dict(os.environ, {}, clear=True)
    def test_rejects_uutils_as_only_wc(self):
        with patch('session.shutil.which', side_effect=lambda name: '/usr/bin/wc' if name == 'wc' else None), \
             patch('session.subprocess.run', return_value=subprocess.CompletedProcess(
                 [], 0, b'wc (uutils coreutils) 0.10.0\n', b'')):
            with self.assertRaisesRegex(SystemExit, 'GNU wc is required'):
                find_gnu_wc()

    @patch.dict(os.environ, {'QWC_CONF_WC': '/custom/wc'}, clear=True)
    def test_explicit_override_is_verified_without_fallback(self):
        with patch('session.shutil.which', return_value='/custom/wc') as which, \
             patch('session.subprocess.run', return_value=subprocess.CompletedProcess(
                 [], 0, b'wc (uutils coreutils) 0.10.0\n', b'')):
            with self.assertRaises(SystemExit):
                find_gnu_wc()
            which.assert_called_once_with('/custom/wc')


if __name__ == '__main__':
    unittest.main()
