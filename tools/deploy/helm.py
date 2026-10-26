"""Produce public Helm qualification values from a validated deployment bundle."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local
from material import verify_bundle
from model import SERVICES, require


def values(bundle, output):
    bundle = Path(bundle).resolve(); profile = verify_bundle(bundle)
    references = sorted({'psim-platform/psim-' + binary + ':' + profile.version for binary, _, _ in SERVICES.values()}
                        | {'psim-platform/' + name + ':' + profile.version for name in ('web', 'haproxy')})
    _, manifest = local.export_images(references)
    result = configuration(bundle, profile, {reference: reference.split(':')[0] + '@' + manifest['oci'][reference][0]
                                           for reference in references})
    output = Path(output); require(not output.exists(), 'Helm values destination exists')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    print('Public Helm values generated; configure target DNS, infrastructure endpoints and unique Secrets before installation')


def configuration(bundle, profile, images):
    result = {'instances': [], 'services': []}
    for node in profile.select('application'):
        for index, (name, (binary, port, _)) in enumerate(SERVICES.items()):
            config = json.loads((bundle / node.name / 'services' / (name + '.json')).read_text())
            config['admin']['listen'] = '0.0.0.0:' + str(19100 + index)
            ports = [] if name in ('normalizer', 'correlation-engine') else [{'name': 'rpc', 'targetPort': port}]
            if name == 'connector-gateway': ports.append({'name': 'management', 'targetPort': port + 10000})
            storage = []
            for field in ('spool_directory', 'state_directory', 'checkpoint_directory'):
                if field in config['settings']:
                    storage.append({'name': field.replace('_', '-'), 'path': config['settings'][field], 'size': '10Gi'})
            reference = 'psim-platform/psim-' + binary + ':' + profile.version
            result['instances'].append({'name': name + '-' + node.name, 'component': name,
                'image': images[reference],
                'uid': profile.uid(name), 'adminPort': 19100 + index, 'materialSecret': name + '-' + node.name,
                'config': config, 'ports': ports, 'storage': storage,
                'resources': {'requests': {'cpu': '100m', 'memory': '128Mi'}, 'limits': {'cpu': '2', 'memory': '2Gi'}}})
    for name, (_, port, _) in SERVICES.items():
        if name in ('normalizer', 'correlation-engine'): continue
        ports = [{'name': 'rpc', 'port': port, 'targetPort': 'rpc'},
                 {'name': 'internal', 'port': port + 10000, 'targetPort': 'management' if name == 'connector-gateway' else 'rpc'}]
        result['services'].append({'component': name, 'ports': ports})
    edge_configuration = f'''global
  maxconn 20000
defaults
  timeout connect 3s
  timeout client 60s
  timeout server 60s
  timeout tunnel 1h
frontend health
  bind 0.0.0.0:19130
  mode http
  http-request return status 200
frontend public
  bind 0.0.0.0:443 ssl crt /etc/psim-platform/haproxy/secrets/public.pem alpn h2,http/1.1 ssl-min-ver TLSv1.3
  mode http
  http-request set-header X-Forwarded-Proto https
  http-request set-header X-Forwarded-Port {profile.public_port}
  http-response set-header Strict-Transport-Security max-age=31536000
  acl identity path_beg /identity/
  acl api path_beg /v1/ /ws
  use_backend identity if identity
  use_backend api if api
  default_backend web
listen connectors
  bind 0.0.0.0:7443
  mode tcp
  server gateway connector-gateway:17443 check
'''
    for name, host, port in [('identity', 'keycloak', 18444), ('api', 'api-gateway', 18013), ('web', 'web', 18180)]:
        edge_configuration += ('backend ' + name + '\n  mode http\n  server upstream ' + host + ':' + str(port)
            + ' ssl verify required ca-file /etc/psim-platform/haproxy/secrets/ca.pem crt /etc/psim-platform/haproxy/secrets/public.pem'
            + ' verifyhost ' + host + ' sni str(' + host + ') check\n')
    web_probe = "const fs=require('node:fs'),https=require('node:https');const read=n=>fs.readFileSync(process.env[n]);https.get({hostname:'127.0.0.1',port:18180,servername:'web',path:'/health/ready',ca:read('PSIM_CA_FILE'),cert:read('PSIM_CERTIFICATE_FILE'),key:read('PSIM_PRIVATE_KEY_FILE'),timeout:2000},r=>{r.resume();r.on('end',()=>process.exit(r.statusCode===200?0:1))}).on('error',()=>process.exit(1)).on('timeout',function(){this.destroy()});"
    for node in profile.select('application'):
        material = '/etc/psim-platform/web/secrets/'
        result['instances'].append({'name': 'web-' + node.name, 'component': 'web', 'image': images['psim-platform/web:' + profile.version],
            'uid': profile.uid('web'), 'adminPort': 18180, 'materialSecret': 'web-' + node.name, 'config': {}, 'args': [],
            'environment': [{'name': key, 'value': value} for key, value in {'PSIM_ISSUER': profile.origin + '/identity/realms/psim',
                'PSIM_LISTEN': '0.0.0.0', 'PSIM_PORT': '18180', 'PSIM_CA_FILE': material + 'ca.pem', 'PSIM_CERTIFICATE_FILE': material + 'identity.pem',
                'PSIM_PRIVATE_KEY_FILE': material + 'identity.key'}.items()], 'probeExec': ['node', '-e', web_probe],
            'ports': [{'name': 'https', 'targetPort': 18180}], 'storage': [],
            'resources': {'requests': {'cpu': '50m', 'memory': '64Mi'}, 'limits': {'cpu': '1', 'memory': '256Mi'}}})
    for node in profile.select('edge'):
        result['instances'].append({'name': 'haproxy-' + node.name, 'component': 'haproxy',
            'image': images['psim-platform/haproxy:' + profile.version], 'uid': profile.uid('haproxy'), 'adminPort': 19130,
            'materialSecret': 'haproxy-' + node.name, 'config': {}, 'configFiles': {'edge.cfg': edge_configuration},
            'args': ['-W', '-db', '-f', '/etc/psim-platform/services/edge.cfg'], 'capabilities': ['NET_BIND_SERVICE'],
            'ports': [{'name': 'https', 'targetPort': 443}, {'name': 'connectors', 'targetPort': 7443}], 'storage': [],
            'resources': {'requests': {'cpu': '50m', 'memory': '64Mi'}, 'limits': {'cpu': '1', 'memory': '256Mi'}}})
    result['services'] += [{'component': 'web', 'ports': [{'name': 'https', 'port': 18180, 'targetPort': 'https'}]},
        {'component': 'haproxy', 'type': 'LoadBalancer', 'ports': [{'name': 'https', 'port': 443, 'targetPort': 'https'},
         {'name': 'connectors', 'port': 7443, 'targetPort': 'connectors'}]}]
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--bundle', required=True); parser.add_argument('--output', required=True)
    args = parser.parse_args(); values(args.bundle, args.output)
