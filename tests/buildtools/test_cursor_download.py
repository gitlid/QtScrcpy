"""Offline checks for the pinned cursor build-tool download retry path."""
import hashlib
import importlib.util
from pathlib import Path
import ssl
import tempfile
import unittest
from unittest.mock import MagicMock, patch
import urllib.error

SOURCE = Path(__file__).resolve().parents[2] / 'ci' / 'build_phone_cursor.py'
spec = importlib.util.spec_from_file_location('cursor_build', SOURCE)
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'ecj.jar'
        self.payload = b'verified fixture, not an actual build tool'
        self.digest = hashlib.sha256(self.payload).hexdigest()
        self.response = MagicMock()
        self.response.__enter__.return_value.read.return_value = self.payload
        self.url = build.TOOLS['ecj.jar'][0]

    def test_transient_retry_keeps_url_and_tls_defaults(self):
        transient = urllib.error.URLError(ConnectionResetError(10054, 'reset'))
        with patch.object(build.urllib.request, 'urlopen', side_effect=[transient, self.response]) as call, patch.object(build.time, 'sleep') as sleep:
            build.download_tool(self.url, self.digest, self.path)
        self.assertEqual(self.path.read_bytes(), self.payload)
        self.assertEqual(call.call_count, 2)
        for request in call.call_args_list:
            self.assertEqual(request.args, (self.url,))
            self.assertEqual(request.kwargs, {'timeout': 120})
        sleep.assert_called_once_with(1)

    def test_retries_are_bounded_and_no_partial_cache(self):
        with patch.object(build.urllib.request, 'urlopen', side_effect=TimeoutError('timeout')) as call, patch.object(build.time, 'sleep') as sleep:
            with self.assertRaises(TimeoutError):
                build.download_tool(self.url, self.digest, self.path)
        self.assertEqual(call.call_count, 4)
        self.assertEqual([c.args[0] for c in sleep.call_args_list], [1, 2, 4])
        self.assertFalse(self.path.exists())

    def test_hash_mismatch_never_retried_or_cached(self):
        with patch.object(build.urllib.request, 'urlopen', return_value=self.response) as call, patch.object(build.time, 'sleep') as sleep:
            with self.assertRaisesRegex(RuntimeError, 'Hash mismatch'):
                build.download_tool(self.url, '0' * 64, self.path)
        self.assertEqual(call.call_count, 1)
        sleep.assert_not_called()
        self.assertFalse(self.path.exists())

    def test_certificate_failure_never_downgraded(self):
        failure = urllib.error.URLError(ssl.SSLCertVerificationError('certificate rejected'))
        with patch.object(build.urllib.request, 'urlopen', side_effect=failure) as call, patch.object(build.time, 'sleep') as sleep:
            with self.assertRaises(urllib.error.URLError):
                build.download_tool(self.url, self.digest, self.path)
        self.assertEqual(call.call_count, 1)
        sleep.assert_not_called()
        self.assertFalse(self.path.exists())

    def test_not_found_never_retried(self):
        failure = urllib.error.HTTPError(self.url, 404, 'Not found', None, None)
        with patch.object(build.urllib.request, 'urlopen', side_effect=failure) as call, patch.object(build.time, 'sleep') as sleep:
            with self.assertRaises(urllib.error.HTTPError):
                build.download_tool(self.url, self.digest, self.path)
        self.assertEqual(call.call_count, 1)
        sleep.assert_not_called()


if __name__ == '__main__':
    unittest.main()
