"""Render both deployment forms and validate chart isolation without creating VMs or a Kubernetes cluster."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))


def render_profiles(output):
    from helm import configuration
    from material import verify_bundle
    from model import Profile, SERVICES, require
    from render import render
    for name in ('single', 'cluster', 'local-single', 'local-vm'):
        profile = Profile.load(ROOT / ('deploy/topologies/' + name + '.json'))
        bundle = output / name; render(profile, bundle); verify_bundle(bundle)
        inventory = json.loads((bundle / 'material.json').read_text())['files']
        require(all(entry['mode'] == '0400' and entry['uid'] > 0 for entry in inventory.values()), 'unsafe material ownership')
        for node in profile.nodes:
            compose = json.loads((bundle / node.name / 'compose.json').read_text())['services']
            require(all(service['read_only'] and service['user'].split(':')[0] != '0'
                        and service['cap_drop'] == ['ALL'] for service in compose.values()), 'unsafe service process isolation')
            if 'application' in node.roles:
                require(set(SERVICES) <= compose.keys(), 'application topology is incomplete')
        references = {'psim-platform/psim-' + binary + ':' + profile.image_version for binary, _, _ in SERVICES.values()}
        references |= {'psim-platform/' + name + ':' + profile.image_version for name in ('web', 'haproxy')}
        # Rendering fixtures never refer to executable images and are removed after the check.
        images = {reference: reference.split(':')[0] + '@sha256:' + '0' * 64 for reference in references}
        (output / ('values-' + name + '.json')).write_text(json.dumps(configuration(bundle, profile, images)))


def check():
    from operators import HELM
    folder = ROOT / 'build/deployment'; folder.mkdir(parents=True, exist_ok=True)
    image = 'psim-platform/toolchain:debian13-' + hashlib.sha256((ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    report = []
    with tempfile.TemporaryDirectory(prefix='.static-', dir=folder) as temporary:
        output = Path(temporary)
        subprocess.run(['docker', 'run', '--rm', '--network', 'none', '--user', str(os.getuid()) + ':' + str(os.getgid()),
            '--mount', 'type=bind,src=' + str(ROOT) + ',dst=/repo', '--entrypoint', 'python3', image,
            '/repo/tools/deploy/check.py', '--render', '/repo/' + str(output.relative_to(ROOT))], check=True)
        helm = ['docker', 'run', '--rm', '--network', 'none', '--read-only', '--cap-drop', 'ALL',
                '--security-opt', 'no-new-privileges:true', '--user', str(os.getuid()) + ':' + str(os.getgid()),
                '--tmpfs', '/tmp:rw,nosuid,size=64m', '--mount', 'type=bind,src=' + str(ROOT) + ',dst=/repo,readonly', HELM]
        for name in ('single', 'cluster', 'local-single', 'local-vm'):
            bundle = output / name
            for node in json.loads((bundle / 'profile.json').read_text())['nodes']:
                subprocess.run(['docker', 'compose', '--file', str(bundle / node['name'] / 'compose.json'), 'config', '--quiet'], check=True)
            values = '/repo/' + str((output / ('values-' + name + '.json')).relative_to(ROOT))
            subprocess.run(helm + ['lint', '--strict', '/repo/deploy/helm/psim-platform', '--values', values], check=True)
            rendered = subprocess.check_output(helm + ['template', 'psim', '/repo/deploy/helm/psim-platform', '--values', values], text=True)
            document = json.loads((output / ('values-' + name + '.json')).read_text())
            if rendered.count('kind: StatefulSet') != len(document['instances']): raise ValueError('chart instance count differs')
            report.append({'profile': name, 'application_instances': len(document['instances']), 'compose': True, 'helm': True})
        document['instances'][1]['materialSecret'] = document['instances'][0]['materialSecret']
        rejected = output / 'reused-secret.json'; rejected.write_text(json.dumps(document))
        result = subprocess.run(helm + ['template', 'psim', '/repo/deploy/helm/psim-platform', '--values',
            '/repo/' + str(rejected.relative_to(ROOT))], capture_output=True, text=True)
        if result.returncode == 0 or 'unique names and private material Secrets' not in result.stderr:
            raise ValueError('chart accepted reused private identity material')
    (folder / 'static-qualification.json').write_text(json.dumps({'profiles': report, 'reused_secret_rejected': True,
        'fixture_digests_only': True, 'live_kubernetes_tested': False}, indent=2) + '\n')
    print('Deployment rendering, process isolation and private chart material checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--render'); args = parser.parse_args()
    if args.render: render_profiles(Path(args.render))
    else: check()
