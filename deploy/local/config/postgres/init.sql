-- Databases of the local environment. Development-only passwords.
-- psim: platform services, one schema per service;
-- registry: Apicurio Registry (ADR-003); keycloak: identity provider.
CREATE ROLE psim LOGIN PASSWORD 'psim-dev-psim';
CREATE ROLE registry LOGIN PASSWORD 'psim-dev-registry';
CREATE ROLE keycloak LOGIN PASSWORD 'psim-dev-keycloak';
CREATE DATABASE psim OWNER psim;
CREATE DATABASE registry OWNER registry;
CREATE DATABASE keycloak OWNER keycloak;
REVOKE ALL ON DATABASE psim, registry, keycloak FROM PUBLIC;
