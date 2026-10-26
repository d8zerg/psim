#!/usr/bin/env python3
"""Derive one product version from reachable SemVer tags and source identity."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?')


def validate(value):
    match = SEMVER.fullmatch(value)
    if not match or any(not part or (part.isdigit() and len(part) > 1 and part[0] == '0') for part in (match[4] or '').split('.') if match[4]):
        raise ValueError('invalid semantic version')
    if match[5] and any(not part for part in match[5].split('.')):
        raise ValueError('invalid build metadata')
    return value


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True, stderr=subprocess.DEVNULL).strip()


def derive(root=ROOT, override=None):
    root = Path(root)
    revision = git(root, 'rev-parse', 'HEAD')
    dirty = bool(git(root, 'status', '--porcelain', '--untracked-files=normal'))
    if override:
        version = validate(override)
        return {'version': version, 'numeric': version.split('-')[0].split('+')[0],
                'debian': version.replace('-', '~', 1), 'image': version.replace('+', '_'),
                'revision': revision, 'dirty': dirty, 'tagged': False}
    tags = []
    for name in git(root, 'tag', '--merged', 'HEAD', '--list').splitlines():
        try:
            validate(name.removeprefix('v'))
            tags.append(name)
        except ValueError:
            continue
    if not tags:
        raise ValueError('no reachable SemVer Git tag; create an initial version tag or explicitly set PSIM_BUILD_VERSION for a development fixture')
    try:
        matches = [argument for tag in tags for argument in ('--match', tag)]
        description = git(root, 'describe', '--tags', '--long', '--abbrev=12', *matches)
        tag, distance, abbreviated = description.rsplit('-', 2)
        tagged = validate(tag.removeprefix('v'))
    except (subprocess.CalledProcessError, ValueError) as error:
        raise ValueError('cannot resolve the nearest reachable SemVer Git tag') from error
    development = tagged.split('+')[0]
    if '-' not in development:
        major, minor, patch = map(int, development.split('.'))
        development = f'{major}.{minor}.{patch + 1}-dev.'
    else:
        development += '.dev.'
    version = (tagged if distance == '0' and not dirty else
               development + distance + '+' + abbreviated + ('.dirty' if dirty else ''))
    validate(version)
    numeric = version.split('-')[0].split('+')[0]
    debian = version.replace('-', '~', 1)
    return {'version': version, 'numeric': numeric, 'debian': debian, 'image': version.replace('+', '_'),
            'revision': revision, 'dirty': dirty,
            'tagged': distance == '0' and not dirty and git(root, 'cat-file', '-t', 'refs/tags/' + tag) == 'tag'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--field', choices=['version', 'numeric', 'debian', 'image', 'revision'])
    parser.add_argument('--release', action='store_true')
    args = parser.parse_args()
    try:
        value = derive(override=os.environ.get('PSIM_BUILD_VERSION'))
        if args.release and not value['tagged']:
            raise ValueError('official release requires a clean annotated SemVer tag')
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'version: ' + str(error) + '\n')
    print(value[args.field] if args.field else json.dumps(value, sort_keys=True))


if __name__ == '__main__':
    main()
