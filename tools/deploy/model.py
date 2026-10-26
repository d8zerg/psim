"""Validated deployment inventory shared by container and systemd rendering."""
from dataclasses import dataclass
import ipaddress
import json
from pathlib import Path
import re
import uuid

SERVICES = {
    'connector-gateway': ('connector-gateway', 17443, 'ingest'),
    'normalizer': ('normalizer', 18001, None),
    'event-history': ('event-history', 18002, 'history'),
    'correlation-engine': ('correlation-engine', 18003, None),
    'rule-management': ('correlation-engine', 18004, 'rules'),
    'incident-service': ('incident-service', 18005, 'incident'),
    'response-engine': ('response-engine', 18006, 'response'),
    'command-service': ('command-service', 18007, 'command'),
    'notification-service': ('notification-service', 18008, 'notification'),
    'resource-catalog': ('resource-catalog', 18009, 'catalog'),
    'projection-service': ('projection-service', 18011, 'reports'),
    'audit-service': ('audit-service', 18012, 'audit'),
    'api-gateway': ('api-gateway', 18013, 'access'),
}
ROLES = {'infrastructure', 'application', 'edge', 'observability'}
NAME = re.compile(r'[a-z][a-z0-9-]{0,47}\Z')
DNS = re.compile(r'[a-zA-Z0-9](?:[a-zA-Z0-9.-]{0,251}[a-zA-Z0-9])?\Z')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identifier(value):
    require(isinstance(value, str) and NAME.fullmatch(value), 'invalid inventory identifier')
    return value


@dataclass(frozen=True)
class Node:
    name: str
    address: str
    domain: str
    roles: frozenset[str]


@dataclass(frozen=True)
class Profile:
    name: str
    topology: str
    nodes: tuple[Node, ...]
    public_name: str
    virtual_ip: str | None
    interface: str
    version: str
    prefix: str
    uid_base: int
    shards: int
    public_port: int
    security_tenant: str

    @classmethod
    def load(cls, path):
        return cls.parse(json.loads(Path(path).read_text()))

    @classmethod
    def parse(cls, value):
        require(isinstance(value, dict), 'inventory must be an object')
        require(set(value) <= {'format', 'name', 'topology', 'nodes', 'public_name', 'public_port', 'security_tenant', 'virtual_ip', 'interface', 'version', 'prefix', 'uid_base', 'shards'}, 'unknown inventory field')
        require(value.get('format') == 1, 'unsupported inventory format')
        topology = value.get('topology')
        require(topology in ('single', 'cluster'), 'topology must be single or cluster')
        nodes = []
        require(isinstance(value.get('nodes'), list), 'inventory nodes must be an array')
        for item in value['nodes']:
            require(isinstance(item, dict) and set(item) == {'name', 'address', 'failure_domain', 'roles'}, 'invalid node fields')
            address = str(ipaddress.IPv4Address(item['address']))
            require(not ipaddress.ip_address(address).is_unspecified and not ipaddress.ip_address(address).is_multicast, 'invalid node address')
            require(isinstance(item['roles'], list) and all(isinstance(role, str) for role in item['roles']), 'invalid node role list')
            roles = frozenset(item['roles'])
            require(roles and roles <= ROLES, 'invalid node roles')
            nodes.append(Node(identifier(item['name']), address, identifier(item['failure_domain']), roles))
        require(nodes and len({n.name for n in nodes}) == len(nodes) and len({n.address for n in nodes}) == len(nodes), 'nodes must have unique names and addresses')
        public = value.get('public_name', '')
        require(isinstance(public, str) and len(public) <= 253 and all(re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?', label) for label in public.split('.')), 'invalid public DNS name')
        version = value.get('version', '')
        require(isinstance(version, str) and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?(?:\+[a-zA-Z0-9.-]+)?', version), 'invalid application version')
        prefix = value.get('prefix', '/var/lib/psim-platform')
        require(isinstance(prefix, str) and re.fullmatch(r'/[a-zA-Z0-9_/-]+', prefix) and '..' not in Path(prefix).parts and prefix not in ('/', '/etc', '/var', '/var/lib'), 'invalid installation prefix')
        uid = value.get('uid_base', 60000)
        shards = value.get('shards', 1)
        require(type(uid) is int and 1000 <= uid <= 64000, 'invalid service UID range')
        require(type(shards) is int and 1 <= shards <= 64, 'invalid shard count')
        interface = value.get('interface', 'eth0')
        require(isinstance(interface, str) and re.fullmatch(r'[a-zA-Z0-9_.-]{1,15}', interface), 'invalid network interface')
        virtual = value.get('virtual_ip')
        if virtual is not None:
            require(str(ipaddress.IPv4Interface(virtual)) == virtual, 'invalid virtual address')
            require(str(ipaddress.IPv4Interface(virtual).ip) not in {n.address for n in nodes}, 'virtual address duplicates a node')
        port = value.get('public_port', 443)
        require(type(port) is int and 1 <= port <= 65535, 'invalid public port')
        tenant = value.get('security_tenant')
        require(isinstance(tenant, str) and str(uuid.UUID(tenant)) == tenant, 'invalid security tenant')
        profile = cls(identifier(value.get('name')), topology, tuple(nodes), public, virtual, interface, version, prefix.rstrip('/'), uid, shards, port, tenant)
        if topology == 'single':
            require(len(nodes) == 1 and nodes[0].roles == ROLES, 'single topology requires one node with all roles')
            require(virtual is None, 'single topology does not use a virtual address')
        else:
            require(len(profile.select('infrastructure')) == 3, 'cluster requires exactly three infrastructure nodes')
            for role, count in [('application', 2), ('edge', 2), ('observability', 1)]:
                require(len(profile.select(role)) >= count, 'insufficient nodes for role ' + role)
            require(len({n.domain for n in profile.select('infrastructure')}) == 3, 'infrastructure nodes require three failure domains')
            for role in ('application', 'edge'):
                require(len({n.domain for n in profile.select(role)}) >= 2, role + ' replicas require distinct failure domains')
            require(virtual is not None, 'cluster requires a virtual address')
        return profile

    def select(self, role):
        return tuple(n for n in self.nodes if role in n.roles)

    @property
    def image_version(self):
        return self.version.replace('+', '_')

    @property
    def package_version(self):
        return self.version.replace('-', '~', 1)

    def document(self):
        return {'format': 1, 'name': self.name, 'topology': self.topology, 'version': self.version,
                'public_name': self.public_name, 'public_port': self.public_port, 'security_tenant': self.security_tenant,
                'virtual_ip': self.virtual_ip, 'interface': self.interface, 'prefix': self.prefix,
                'uid_base': self.uid_base, 'shards': self.shards,
                'nodes': [{'name': n.name, 'address': n.address, 'failure_domain': n.domain, 'roles': sorted(n.roles)} for n in self.nodes]}

    def uid(self, service):
        names = list(SERVICES) + ['postgres', 'etcd', 'valkey', 'sentinel', 'kafka', 'keeper', 'clickhouse', 'keycloak', 'registry', 'seaweed', 'haproxy', 'web', 'observability', 'deployment-admin', 'monitoring', 'prometheus', 'alertmanager', 'grafana', 'loki', 'tempo']
        return self.uid_base + names.index(service)

    @property
    def issuer(self):
        return self.origin + '/identity/realms/psim'

    @property
    def origin(self):
        return 'https://' + self.public_name + (':' + str(self.public_port) if self.public_port != 443 else '')
