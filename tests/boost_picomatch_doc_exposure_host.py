#!/usr/bin/env python3
"""Pin scope of CVE-2026-112366 to imported Boost Antora docs locks.

This is an exposure inventory, NOT a dependency patch or a Dependabot dismissal.
It intentionally does not install npm dependencies or run vulnerable patterns.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_ROOT = ROOT / 'sources/eden-cpm/boost/boost-1.90.0/libs'
EXPECTED = {
    'dynamic_bitset': ('4.0.7', '2.3.2'),
    'msm': ('2.3.2',),
    'openmethod': ('4.0.7', '2.3.2'),
    'redis': ('4.0.7', '2.3.2'),
    'unordered': ('4.0.7', '2.3.2'),
    'url': ('4.0.7', '2.3.2'),
}

found = {}
for lockfile in DOC_ROOT.rglob('package-lock.json'):
    lock = json.loads(lockfile.read_text(encoding='utf-8'))
    packages = lock.get('packages', {})
    matches = {
        key: pkg for key, pkg in packages.items()
        if key.rsplit('/', 1)[-1] == 'picomatch'
    }
    if not matches:
        continue
    library = lockfile.relative_to(DOC_ROOT).parts
    assert len(library) == 3 and library[1:] == ('doc', 'package-lock.json'), (
        'Picomatch lock outside docs-only source', lockfile)
    name = library[0]
    assert name in EXPECTED, ('New picomatch lock needs security triage', str(lockfile))
    assert name not in found, ('Duplicate picomatch lock', str(lockfile))
    assert lock.get('lockfileVersion') == 3, (str(lockfile), 'lockfileVersion drift')
    package_json = json.loads(lockfile.with_name('package.json').read_text(encoding='utf-8'))
    deps = {**package_json.get('dependencies', {}), **package_json.get('devDependencies', {})}
    assert any(key == 'antora' or key.startswith('@antora/') for key in deps), (
        'No longer a recognized Antora docs dependency graph', name)
    assert 'picomatch' not in deps, ('Picomatch changed from transitive to direct', name)
    versions = tuple(sorted(pkg.get('version') for pkg in matches.values()))
    assert versions == tuple(sorted(EXPECTED[name])), (
        'New version/graph must be reviewed against GHSA-fc5q-39xc-vjpf', name, versions)
    for module, pkg in matches.items():
        ver = pkg['version']
        assert pkg.get('resolved') == f'https://registry.npmjs.org/picomatch/-/picomatch-{ver}.tgz', (
            'npm package source drift', name, module)
        assert pkg.get('integrity', '').startswith('sha512-'), ('Missing npm integrity', name, module)
    found[name] = len(matches)

assert set(found) == set(EXPECTED), (
    'Expected Boost doc lock missing or review baseline changed',
    sorted(set(EXPECTED) - set(found)), sorted(set(found) - set(EXPECTED)))
print('BOOST_PICOMATCH_DOC_EXPOSURE_INVENTORY: '
      f'locks={len(found)} transitive_instances={sum(found.values())} '
      'CVE-2026-112366=OPEN patched_release=NONE native_runtime_status=NOT_PROVEN_BY_THIS_TEST')
