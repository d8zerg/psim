#!/usr/bin/env python3
"""Render and inspect a deployment bundle without modifying the target machine."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import Profile
from render import render


def main():
    parser = argparse.ArgumentParser(prog='psim-deploy')
    actions = parser.add_subparsers(dest='action', required=True)
    command = actions.add_parser('render')
    command.add_argument('--profile', required=True)
    command.add_argument('--output', required=True)
    command = actions.add_parser('inspect')
    command.add_argument('--bundle', required=True)
    args = parser.parse_args()
    if args.action == 'render':
        profile = Profile.load(args.profile)
        render(profile, args.output)
        print('deployment bundle rendered: ' + profile.name + ' (' + profile.topology + ')')
    else:
        manifest = json.loads((Path(args.bundle) / 'manifest.json').read_text())
        print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, json.JSONDecodeError) as error:
        print('psim-deploy: ' + str(error), file=sys.stderr)
        sys.exit(1)
