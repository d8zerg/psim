"""Install a fresh node payload on its target; existing installations are immutable."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import pwd
import grp
import shutil
import stat
import subprocess
import tarfile
import tempfile


def require(condition, message):
    if not condition: raise ValueError(message)


def run(*args):
    subprocess.run(args, check=True)


def watchdog(uid):
    for name in ('/dev/watchdog', '/dev/watchdog0'):
        device = Path(name)
        require(device.exists() and stat.S_ISCHR(device.stat().st_mode), 'hardware watchdog device is missing')
        os.chown(device, uid, uid); device.chmod(0o600)
    rules = Path('/etc/udev/rules.d/70-psim-watchdog.rules')
    rules.write_text('KERNEL=="watchdog*", OWNER="' + str(uid) + '", GROUP="' + str(uid) + '", MODE="0600"\n')
    run('udevadm', 'control', '--reload-rules')


def account(owner, uid):
    name = 'psim-' + owner
    try:
        current = pwd.getpwnam(name)
        require(current.pw_uid == uid and current.pw_gid == uid and pwd.getpwuid(uid).pw_name == name and grp.getgrnam(name).gr_gid == uid, 'service UID conflict')
    except KeyError:
        try: pwd.getpwuid(uid); raise ValueError('service UID already exists')
        except KeyError: pass
        try: require(grp.getgrnam(name).gr_gid == uid and grp.getgrgid(uid).gr_name == name, 'service GID conflict')
        except KeyError:
            try: grp.getgrgid(uid); raise ValueError('service GID already exists')
            except KeyError: pass
            run('groupadd', '--system', '--gid', str(uid), name)
        run('useradd', '--system', '--uid', str(uid), '--gid', str(uid), '--no-create-home', '--home-dir', '/nonexistent', '--shell', '/usr/sbin/nologin', name)


def install(archive, digest, expected_node):
    require(os.geteuid() == 0, 'node installation requires root on the target VM')
    archive = Path(archive)
    require(hashlib.sha256(archive.read_bytes()).hexdigest() == digest, 'node payload checksum mismatch')
    with tarfile.open(archive, 'r:gz') as package:
        members = package.getmembers()
        names = [member.name for member in members]
        require(len(names) == len(set(names)) and 'payload.json' in names, 'invalid node payload member set')
        for member in members:
            path = PurePosixPath(member.name)
            require(not path.is_absolute() and '..' not in path.parts and member.isfile(), 'unsafe node payload member')
        metadata = json.load(package.extractfile('payload.json'))
        require(metadata['format'] == 1 and metadata['node'] == expected_node, 'node payload identity mismatch')
        require(set(names) == set(metadata['files']) | {'payload.json'}, 'node payload inventory mismatch')
        for name, entry in metadata['files'].items():
            require(hashlib.sha256(package.extractfile(name).read()).hexdigest() == entry['sha256'], 'node payload member checksum mismatch')
        prefix = Path(metadata['profile']['prefix'])
        require(prefix.is_absolute() and '..' not in prefix.parts and len(prefix.parts) >= 3, 'invalid installation prefix')
        require(not any(path.is_symlink() for path in (prefix, *prefix.parents)), 'installation prefix traverses a symlink')
        state = prefix / 'installation.json'
        pg_uid = metadata['directories'].get('data/' + expected_node + '/postgres') if metadata['profile']['topology'] == 'cluster' else None
        if pg_uid is not None:
            require(Path('/dev/watchdog').exists() and stat.S_ISCHR(Path('/dev/watchdog').stat().st_mode), 'hardware watchdog is required on PostgreSQL cluster nodes')
        if prefix.exists():
            require(state.is_file(), 'installation prefix is already in use')
            installed = json.loads(state.read_text())
            require(installed['sha256'] == digest and installed['node'] == expected_node, 'existing installation differs; initial install cannot replace it')
            if pg_uid is not None: watchdog(pg_uid)
            os.sync()
            print('node payload already installed: ' + expected_node)
            return
        prefix.parent.mkdir(parents=True, exist_ok=True)
        temporary = Path(tempfile.mkdtemp(prefix='.psim-install-', dir=prefix.parent))
        try:
            package.extractall(temporary, filter='data')
            for name, entry in metadata['files'].items():
                path = temporary / name
                path.chmod(entry['mode']); os.chown(path, entry['uid'], entry['uid'])
                if name.startswith('material/'):
                    owner = Path(name).parts[2]
                    account(owner, entry['uid'])
                    path.parent.chmod(0o700); os.chown(path.parent, entry['uid'], entry['uid'])
            for relative, uid in metadata['directories'].items():
                path = PurePosixPath(relative)
                require(not path.is_absolute() and '..' not in path.parts and path.parts[0] in ('data', 'material'), 'invalid private data directory')
                target = temporary / relative
                target.mkdir(parents=True, exist_ok=True, mode=0o700)
                target.chmod(0o700); os.chown(target, uid, uid)
            (temporary / 'installation.json').write_text(json.dumps({'sha256': digest, 'name': metadata['profile']['name'], 'node': expected_node}) + '\n')
            (temporary / 'installation.json').chmod(0o444)
            (temporary / 'payload.json').chmod(0o400)
            temporary.chmod(0o755)
            temporary.rename(prefix)
        finally:
            if temporary.exists(): shutil.rmtree(temporary)
    if pg_uid is not None: watchdog(pg_uid)
    os.sync()
    print('fresh node payload installed: ' + expected_node)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', required=True); parser.add_argument('--sha256', required=True); parser.add_argument('--node', required=True)
    args = parser.parse_args()
    install(args.archive, args.sha256, args.node)
