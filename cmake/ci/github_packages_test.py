#!/usr/bin/env python3
"""Offline regression tests using real curl and a local authenticated NuGet feed."""
import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import unittest
from unittest.mock import patch
import zipfile

from github_packages import BinaryCache, PackageMissing, Packages, restore_sdks, version_key


class Feed:
    def __init__(self):
        self.documents = {}
        self.requests = []
        feed = self
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                feed.requests.append(self.path)
                expected = 'Basic ' + base64.b64encode(b'qigao:test-token').decode()
                if self.headers.get('Authorization') != expected:
                    self.send_error(401)
                    return
                if self.path not in feed.documents:
                    self.send_error(404)
                    return
                data = feed.documents[self.path]
                if not isinstance(data, bytes):
                    data = json.dumps(data).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            def log_message(self, *_):
                pass
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.base = f'http://127.0.0.1:{self.server.server_port}'
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.documents['/index.json'] = {'resources': [{'@id': self.base + '/flat/', '@type': 'PackageBaseAddress/3.0.0'}]}

    def close(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()

    def add(self, package, version, files, spec_id=None):
        package = package.lower()
        index = self.documents.setdefault('/flat/' + package + '/index.json', {'versions': []})
        index['versions'].append(version)
        data = io.BytesIO()
        with zipfile.ZipFile(data, 'w') as archive:
            archive.writestr(package + '.nuspec', f'<package xmlns="urn:nuget"><metadata><id>{spec_id or package}</id><version>{version}</version></metadata></package>')
            archive.writestr('[Content_Types].xml', 'NuGet metadata')
            for name, content in files.items():
                archive.writestr(name, content)
        path = f'/flat/{package}/{version.lower()}/{package}.{version.lower()}.nupkg'
        self.documents[path] = data.getvalue()
        return self.base + path


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.feed = Feed()
        self.packages = Packages(self.feed.base + '/index.json', 'test-token')
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.feed.close()
        self.tmp.cleanup()

    def test_authenticated_curl_version_selection_and_extraction(self):
        for version in ['4.3.0-rc.9', '4.3.0-rc.11', '4.2.0', '4.3.0-rc.2']:
            self.feed.add('SDK.Native', version, {'sdk/linux-x64/config.cmake': version})
        self.assertEqual(self.packages.select('SDK.Native'), '4.3.0-rc.11')
        self.assertEqual(self.packages.select('SDK.Native', stable=True), '4.2.0')
        self.assertEqual(self.packages.select('SDK.Native', '4.3.0-rc.9'), '4.3.0-rc.9')
        root = self.packages.extract('SDK.Native', '4.3.0-rc.11', self.root)
        self.assertEqual((root / 'sdk/linux-x64/config.cmake').read_text(), '4.3.0-rc.11')
        self.assertLess(version_key('1.2.3-1'), version_key('1.2.3-a'))
        self.assertLess(version_key('1.2.3-rc.11'), version_key('1.2.3'))

    def test_paginated_github_registration_metadata(self):
        url = self.feed.add('sdk.native', '1.2.3', {'sdk/file': 'okay'})
        self.feed.documents['/index.json'] = {'resources': [{'@id': self.feed.base + '/registration/', '@type': 'RegistrationsBaseUrl'}]}
        self.feed.documents['/registration/sdk.native/index.json'] = {'items': [{'@id': self.feed.base + '/page.json'}]}
        self.feed.documents['/page.json'] = {'items': [{'catalogEntry': {'version': '1.2.3'}, 'packageContent': url}]}
        version = self.packages.select('sdk.native')
        self.packages.package('sdk.native', version, self.root / 'sdk.nupkg')
        self.assertIn('/page.json', self.feed.requests)

    def test_rejects_wrong_identity_and_path_escape_before_extraction(self):
        self.feed.add('wrong.native', '1.0.0', {'safe': 'x'}, spec_id='other.native')
        with self.assertRaisesRegex(ValueError, 'identity/version'):
            self.packages.extract('wrong.native', '1.0.0', self.root)
        self.feed.add('escape.native', '1.0.0', {'../outside': 'x'})
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.packages.extract('escape.native', '1.0.0', self.root)
        self.assertFalse((self.root / 'outside').exists())
        self.assertFalse((self.root / 'escape.native/1.0.0').exists())

    def test_sdk_environment_is_published_only_after_complete_validation(self):
        for package, cmake, version in [('Salts.Native', 'Salts', '2.3.0-rc.17'),
                                        ('SaltsUtils.Native', 'SaltsUtils', '4.3.0-rc.11'),
                                        ('CHttp.Native', 'Chttp', '1.0.0')]:
            self.feed.add(package, version, {f'sdk/linux-x64/lib/cmake/{cmake}/{cmake}Config.cmake': ''})
        args = argparse.Namespace(core_only=True, only_chttp=False, salts_utils_version='4.3.0-rc.11', packages=self.root / 'packages', rid='windows-x64')
        environment = self.root / 'env'
        with patch.dict(os.environ, {'GITHUB_ENV': str(environment)}):
            with self.assertRaisesRegex(ValueError, 'does not provide SDK'):
                restore_sdks(args, self.packages)
            self.assertFalse(environment.exists())
            args.rid = 'linux-x64'
            restore_sdks(args, self.packages)
        values = environment.read_text()
        self.assertIn('SALTS_UTILS_NATIVE_VERSION=4.3.0-rc.11', values)
        self.assertNotIn('TURBOSCRIPT_ROOT', values)

    def test_exact_abi_cache_conversion_strips_nuget_metadata_and_reuses_zip(self):
        evidence = b'compiler exact\nport exact\n'
        abi = hashlib.sha256(evidence).hexdigest()
        files = {'CONTROL': 'Package: test', 'BUILD_INFO': 'CRTLinkage: dynamic',
                 'share/test/vcpkg_abi_info.txt': evidence, 'lib/test.a': b'archive',
                 '_rels/.rels': 'NuGet metadata', 'package/services/data/metadata/core-properties/x': 'NuGet metadata'}
        self.feed.add('test_x64-linux', '2026.7.1-vcpkg' + abi, files)
        cache = BinaryCache(self.packages, self.root / 'cache')
        result = cache.get('test', 'x64-linux', abi)
        with zipfile.ZipFile(result) as archive:
            self.assertEqual(archive.read('lib/test.a'), b'archive')
            self.assertNotIn('[Content_Types].xml', archive.namelist())
            self.assertFalse(any(p.endswith('.nuspec') or p.startswith(('_rels/', 'package/')) for p in archive.namelist()))
        requests = len(self.feed.requests)
        self.assertEqual(cache.get('test', 'x64-linux', abi), result)
        self.assertEqual(len(self.feed.requests), requests)
        with self.assertRaises(PackageMissing):
            cache.get('test', 'x64-linux', '0' * 64)

    def test_cache_rejects_abi_evidence_mismatch(self):
        abi = 'a' * 64
        self.feed.add('test_x64-linux', '1.0.0-vcpkg' + abi,
                      {'share/test/vcpkg_abi_info.txt': b'wrong evidence'})
        cache = BinaryCache(self.packages, self.root / 'cache')
        with self.assertRaisesRegex(ValueError, 'ABI evidence mismatch'):
            cache.get('test', 'x64-linux', abi)
        self.assertFalse(list((self.root / 'cache').rglob('*.zip')))
        self.assertFalse(list((self.root / 'cache').rglob('*.tmp')))

    def test_authentication_failure_and_cross_origin_metadata_do_not_expose_token(self):
        client = Packages(self.feed.base + '/index.json', 'wrong-secret')
        with self.assertRaises(PackageMissing) as error:
            client.versions('sdk.native')
        self.assertNotIn('wrong-secret', str(error.exception))
        with self.assertRaisesRegex(ValueError, 'configured feed'):
            self.packages.download('https://example.invalid/package.nupkg', self.root / 'unexpected')
        self.assertFalse((self.root / 'unexpected').exists())


if __name__ == '__main__':
    unittest.main()
