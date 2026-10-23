#!/usr/bin/env python3
"""Read GitHub NuGet packages with curl; no NuGet client, .NET or Mono.

SDK consumers use registration metadata rather than guessing package URLs.
The read-only vcpkg HTTP adapter preserves the upstream NuGet package ID and
exact ABI, while exposing a plain zip to vcpkg's native HTTP provider.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import socketserver
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlsplit
import xml.etree.ElementTree as ET
import zipfile

FEED = 'https://nuget.pkg.github.com/qigao/index.json'
IDENTIFIER = re.compile(r'^[a-zA-Z0-9_.-]+$')
VERSION = re.compile(r'^(\d+)\.(\d+)\.(\d+)(?:\.([0-9]+))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$')


def version_key(version):
    match = VERSION.fullmatch(version)
    if not match:
        raise ValueError('Unsupported package version: ' + version)
    major, minor, patch, revision, prerelease = match.groups()
    suffix = tuple((0, int(p)) if p.isdigit() else (1, p.lower())
                   for p in (prerelease or '').split('.'))
    return (int(major), int(minor), int(patch), int(revision or 0),
            prerelease is None, suffix)


class PackageMissing(Exception):
    pass


class Packages:
    def __init__(self, feed=FEED, token=None):
        self.feed = feed
        self.token = os.environ.get('GITHUB_TOKEN', '') if token is None else token
        if not self.token or any(c in self.token for c in '\r\n"\\'):
            raise ValueError('GITHUB_TOKEN with read:packages permission is required')
        self.resources = None
        self.catalogs = {}
        self.lock = threading.RLock()

    def download(self, url, destination):
        # Only the configured HTTPS feed receives credentials. Local HTTP is
        # used exclusively by the offline regression test feed.
        parsed = urlsplit(url)
        origin = urlsplit(self.feed)
        if (parsed.scheme, parsed.netloc) != (origin.scheme, origin.netloc):
            raise ValueError('Package URL does not belong to the configured feed')
        if parsed.scheme != 'https' and parsed.hostname != '127.0.0.1':
            raise ValueError('Package feed requires HTTPS')
        result = subprocess.run([
            'curl', '--config', '-', '--fail', '--location', '--silent', '--show-error',
            '--retry', '3', '--connect-timeout', '20', '--max-time', '300',
            '--output', str(destination), url,
        ], input=f'user = "qigao:{self.token}"\n', text=True, capture_output=True)
        if result.returncode == 22:
            # Do not echo curl's command or credentials in exceptions/logs.
            raise PackageMissing('Package request failed: ' + parsed.path)
        if result.returncode:
            raise RuntimeError('curl package download failed (exit ' + str(result.returncode) + ')')

    def read_json(self, url):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'metadata.json'
            self.download(url, path)
            return json.loads(path.read_text(encoding='utf-8-sig'))

    def versions(self, package):
        if not IDENTIFIER.fullmatch(package):
            raise ValueError('Invalid package ID')
        package = package.lower()
        with self.lock:
            if package in self.catalogs:
                return self.catalogs[package]
            if self.resources is None:
                self.resources = self.read_json(self.feed)['resources']
            def resources(kind):
                return [r for r in self.resources if any(t == kind or t.startswith(kind + '/')
                        for t in ([r['@type']] if isinstance(r['@type'], str) else r['@type']))]
            bases = [r['@id'] for r in resources('PackageBaseAddress')]
            if bases:
                base = bases[0].rstrip('/') + '/' + package + '/'
                catalog = {v: base + quote(v.lower()) + '/' + package + '.' + quote(v.lower()) + '.nupkg'
                           for v in self.read_json(base + 'index.json')['versions']}
            else:
                registrations = [r['@id'] for r in sorted(resources('RegistrationsBaseUrl'),
                                 key=lambda r: str(r['@type']), reverse=True)]
                if not registrations:
                    raise ValueError('Feed has no NuGet v3 package download metadata')
                index = self.read_json(registrations[0].rstrip('/') + '/' + package + '/index.json')
                catalog = {}
                for page in index['items']:
                    if 'items' not in page:
                        page = self.read_json(page['@id'])
                    for leaf in page['items']:
                        entry = leaf['catalogEntry']
                        if isinstance(entry, str):
                            entry = self.read_json(entry)
                        if entry.get('listed', True):
                            catalog[entry['version']] = leaf.get('packageContent') or entry['packageContent']
            self.catalogs[package] = catalog
            return catalog

    def select(self, package, version=None, stable=False):
        catalog = self.versions(package)
        if version:
            candidates = [v for v in catalog if v.lower() == version.lower()]
        else:
            candidates = [v for v in catalog if not stable or '-' not in v]
        if not candidates:
            raise PackageMissing('No matching version for ' + package)
        return max(candidates, key=version_key)

    def package(self, package, version, destination):
        catalog = self.versions(package)
        if version not in catalog:
            raise PackageMissing('Package version is absent')
        self.download(catalog[version], destination)
        with zipfile.ZipFile(destination) as archive:
            specs = [p for p in archive.namelist() if '/' not in p and p.endswith('.nuspec')]
            if len(specs) != 1:
                raise ValueError('Package must contain one root nuspec')
            root = ET.fromstring(archive.read(specs[0]))
            metadata = {e.tag.split('}')[-1]: e.text for e in root.iter()}
            if (metadata.get('id', '').lower() != package.lower()
                    or metadata.get('version', '').lower() != version.lower()):
                raise ValueError('Downloaded package identity/version mismatch')
            for member in archive.infolist():
                path = PurePosixPath(member.filename)
                if (path.is_absolute() or '..' in path.parts or '\\' in member.filename
                        or ':' in member.filename or (member.external_attr >> 16) & 0o170000 == 0o120000):
                    raise ValueError('Unsafe package member')
        return destination

    def extract(self, package, version, packages):
        target = packages / package.lower() / version
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as tmp:
            tmp = Path(tmp)
            archive_path = self.package(package, version, tmp / 'package.nupkg')
            unpacked = tmp / 'unpacked'
            with zipfile.ZipFile(archive_path) as archive:
                archive.extractall(unpacked)
                for member in archive.infolist():
                    mode = (member.external_attr >> 16) & 0o777
                    if mode and not member.is_dir():
                        (unpacked / member.filename).chmod(mode)
            if target.exists():
                shutil.rmtree(target)
            unpacked.rename(target)
        print(f'Resolved {package}/{version}')
        return target


def export(values, path=None):
    for key, value in values.items():
        value = str(value)
        if '\n' in value or '\r' in value:
            raise ValueError('Invalid environment value')
        print(f'{key}={value}')
        if os.environ.get('GITHUB_ENV'):
            with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as out:
                out.write(f'{key}={value}\n')
    if path and os.environ.get('GITHUB_PATH'):
        with open(os.environ['GITHUB_PATH'], 'a', encoding='utf-8') as out:
            out.write(str(path) + '\n')


def restore_sdks(args, packages):
    dependencies = [
        ('SALTS_ROOT', 'SALTS_NATIVE_VERSION', 'Salts.Native', 'Salts', None, False),
        ('SALTS_UTILS_ROOT', 'SALTS_UTILS_NATIVE_VERSION', 'SaltsUtils.Native', 'SaltsUtils', args.salts_utils_version, False),
        ('CHTTP_ROOT', 'CHTTP_NATIVE_VERSION', 'CHttp.Native', 'Chttp', None, False),
    ]
    if args.only_chttp:
        dependencies = [dependencies[2]]
    if not args.core_only and not args.only_chttp:
        dependencies.append(('TURBOSCRIPT_ROOT', 'TURBOSCRIPT_NATIVE_VERSION', 'TurboScript.Native', 'TurboScript', None, True))
    values = {}
    for variable, version_variable, package, cmake, pin, stable in dependencies:
        version = packages.select(package, pin, stable)
        root = packages.extract(package, version, args.packages) / 'sdk' / args.rid
        if not (root / 'lib/cmake' / cmake / (cmake + 'Config.cmake')).is_file():
            raise ValueError(package + ' does not provide SDK for ' + args.rid)
        values[variable] = root.resolve()
        values[version_variable] = version
    # Publish only when every SDK has been validated.
    export(values)


def restore_re2c(args, packages):
    rid = {('Linux', 'X64'): 'linux-x64', ('Linux', 'ARM64'): 'linux-arm64',
           ('macOS', 'X64'): 'macos-x64', ('macOS', 'ARM64'): 'macos-arm64',
           ('Windows', 'X64'): 'windows-x64'}[(os.environ['RUNNER_OS'], os.environ['RUNNER_ARCH'])]
    version = packages.select('Qigao.Re2c.Binary', stable=True)
    root = packages.extract('Qigao.Re2c.Binary', version, args.packages) / 'tools' / rid
    exe = root / 'bin' / ('re2c.exe' if os.name == 'nt' else 're2c')
    for required in [exe, root / 'share/re2c/stdlib/unicode_categories.re', root / 'share/re2c/stdlib/unicode_properties.re']:
        if not required.is_file():
            raise ValueError('Incomplete re2c package: ' + str(required))
    exe.chmod(exe.stat().st_mode | 0o111)
    subprocess.run([str(exe), '--version'], check=True)
    export({'RE2C_ROOT': root.resolve(), 'RE2C_PACKAGE_VERSION': version}, root.resolve() / 'bin')


class BinaryCache:
    def __init__(self, packages, cache):
        self.packages, self.cache = packages, cache
        self.lock = threading.Lock()

    def get(self, name, triplet, abi):
        if not all(IDENTIFIER.fullmatch(v) for v in (name, triplet)) or not re.fullmatch('[0-9a-f]{64}', abi):
            raise ValueError('Invalid vcpkg cache identity')
        destination = self.cache / abi[:2] / (abi + '.zip')
        with self.lock:
            if destination.is_file():
                return destination
            package = name + '_' + triplet
            candidates = [v for v in self.packages.versions(package) if v.endswith('-vcpkg' + abi)]
            if len(candidates) != 1:
                raise PackageMissing('No unique binary package for exact ABI')
            with tempfile.TemporaryDirectory() as tmp:
                source = self.packages.package(package, candidates[0], Path(tmp) / 'binary.nupkg')
                destination.parent.mkdir(parents=True, exist_ok=True)
                temporary = destination.with_suffix('.tmp')
                try:
                    with zipfile.ZipFile(source) as archive, zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as out:
                        evidence = archive.read(f'share/{name}/vcpkg_abi_info.txt')
                        if hashlib.sha256(evidence).hexdigest() != abi:
                            raise ValueError('Binary package ABI evidence mismatch')
                        for member in archive.infolist():
                            filename = member.filename
                            if (filename == '[Content_Types].xml' or filename == '.signature.p7s'
                                    or filename.endswith('.nuspec') or filename.startswith(('_rels/', 'package/'))):
                                continue
                            out.writestr(member, archive.read(member))
                    temporary.replace(destination)
                finally:
                    temporary.unlink(missing_ok=True)
            print(f'curl restored {package} ABI {abi}', flush=True)
            return destination


class LoopbackServer(ThreadingHTTPServer):
    def server_bind(self):
        # HTTPServer's getfqdn() can block on reverse DNS on macOS runners.
        # This adapter binds only a numeric loopback address and needs no DNS.
        socketserver.TCPServer.server_bind(self)
        self.server_name = '127.0.0.1'
        self.server_port = self.server_address[1]


def cache_server(cache):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            self.respond(True)
        def do_HEAD(self):
            self.respond(False)
        def respond(self, body):
            parts = [unquote(p) for p in urlsplit(self.path).path.strip('/').split('/')]
            try:
                if len(parts) != 4:
                    raise ValueError('Invalid cache request')
                name, triplet, _version, abi = parts
                path = cache.get(name, triplet, abi)
                self.send_response(200)
                self.send_header('Content-Length', str(path.stat().st_size))
                self.end_headers()
                if body:
                    with path.open('rb') as source:
                        shutil.copyfileobj(source, self.wfile)
            except PackageMissing:
                self.send_error(404, 'Exact ABI is not cached')
            except Exception as error:
                print(type(error).__name__ + ': binary cache request failed', flush=True)
                self.send_error(502, 'Binary cache download failed')
        def log_message(self, *_):
            pass
    return LoopbackServer(('127.0.0.1', 0), Handler)


def serve(args, packages):
    server = cache_server(BinaryCache(packages, args.packages))
    ready = args.ready.with_suffix('.tmp')
    ready.write_text(json.dumps({'port': server.server_port, 'pid': os.getpid()}))
    ready.replace(args.ready)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['sdks', 're2c', 'serve-cache'])
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--rid', choices=['linux-x64', 'windows-x64', 'macos-arm64', 'android-arm64-v8a'])
    parser.add_argument('--core-only', action='store_true')
    parser.add_argument('--only-chttp', action='store_true')
    parser.add_argument('--salts-utils-version', default='4.3.0-rc.11')
    parser.add_argument('--ready', type=Path)
    args = parser.parse_args()
    args.packages = args.packages.resolve()
    packages = Packages()
    if args.command == 'sdks':
        if not args.rid:
            parser.error('--rid is required for SDK downloads')
        restore_sdks(args, packages)
    elif args.command == 're2c':
        restore_re2c(args, packages)
    else:
        if not args.ready:
            parser.error('--ready is required for the cache adapter')
        serve(args, packages)


if __name__ == '__main__':
    main()
