"""Normalize protobuf and REST contracts for signed release compatibility checks."""
import argparse
import json
from pathlib import Path
import yaml


def snapshot(root, descriptor, admin_fields):
    root = Path(root)
    messages, enums, services = {}, {}, {}
    def walk(container, prefix):
        for message in container.get('messageType', container.get('nestedType', [])):
            name = prefix + '.' + message['name']
            messages[name] = {str(field['number']): {key: field.get(key) for key in
                ('name', 'type', 'typeName', 'label', 'oneofIndex', 'proto3Optional', 'jsonName', 'options')} for field in message.get('field', [])}
            walk({'nestedType': message.get('nestedType', []), 'enumType': message.get('enumType', [])}, name)
        for enum in container.get('enumType', []):
            enums[prefix + '.' + enum['name']] = {value['name']: value['number'] for value in enum['value']}
    image = json.loads(Path(descriptor).read_text())
    for file in image['file']:
        if file.get('package', '').startswith('psim.'):
            walk(file, file['package'])
            for service in file.get('service', []):
                services[file['package'] + '.' + service['name']] = {
                    method['name']: {key: method.get(key) for key in ('inputType', 'outputType', 'clientStreaming', 'serverStreaming')}
                    for method in service.get('method', [])}
    api = yaml.safe_load((root / 'contracts/openapi/psim-api-v1.yaml').read_text())
    return {'format': 1, 'messages': messages, 'enums': enums, 'services': services, 'paths': api['paths'],
            'schemas': api['components']['schemas'], 'admin_info_fields': sorted(admin_fields)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True); parser.add_argument('--descriptor', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--admin-fields', default='service,instance,version,commit,compiler,build_type,configuration_format')
    args = parser.parse_args()
    Path(args.output).write_text(json.dumps(snapshot(args.root, args.descriptor, args.admin_fields.split(',')), sort_keys=True, indent=2) + '\n')
