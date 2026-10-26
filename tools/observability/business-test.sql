BEGIN;
CREATE ROLE monitoring LOGIN NOSUPERUSER NOBYPASSRLS;
GRANT pg_monitor TO monitoring;
CREATE SCHEMA incident;
CREATE SCHEMA ingest;
CREATE SCHEMA catalog;
CREATE SCHEMA escalation;
CREATE TABLE incident.incidents(tenant_id uuid,body jsonb);
CREATE TABLE ingest.registrations(tenant_id uuid,connector_id uuid,body jsonb DEFAULT '{"state":"RESOURCE_STATE_ACTIVE"}');
CREATE TABLE ingest.sessions(connector_id uuid,online boolean,expires_at timestamptz);
CREATE TABLE catalog.resources(tenant uuid,id uuid,kind text,body jsonb);
CREATE TABLE escalation.duties(tenant uuid,site uuid,body jsonb);
INSERT INTO incident.incidents VALUES
  ('00000000-0000-0000-0000-000000000001','{"state":"INCIDENT_STATE_NEW","summary":"private"}'),
  ('00000000-0000-0000-0000-000000000002','{"state":"INCIDENT_STATE_CLOSED","summary":"private"}');
INSERT INTO ingest.registrations VALUES
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000010'),
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000011');
INSERT INTO ingest.registrations VALUES
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000012','{"state":"RESOURCE_STATE_ARCHIVED"}');
INSERT INTO ingest.sessions VALUES
  ('00000000-0000-0000-0000-000000000010',true,now()+interval '10 minutes'),
  ('00000000-0000-0000-0000-000000000011',true,now()-interval '1 minute');
INSERT INTO catalog.resources VALUES
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000100','location','{"kind":"LOCATION_KIND_SITE"}'),
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000101','location','{"kind":"LOCATION_KIND_SITE"}'),
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000102','location','{"kind":"LOCATION_KIND_SITE"}');
INSERT INTO catalog.resources VALUES
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000103','location','{"kind":"LOCATION_KIND_SITE","state":"RESOURCE_STATE_ARCHIVED"}');
INSERT INTO escalation.duties VALUES
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000100','{"userId":"assigned","fallbackUserId":"private"}'),
  ('00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000101','{"fallbackUserId":"private"}');
ALTER TABLE incident.incidents ENABLE ROW LEVEL SECURITY;
ALTER TABLE incident.incidents FORCE ROW LEVEL SECURITY;
\ir /repo/deploy/observability/business.sql
SET ROLE monitoring;
SET LOCAL transaction_read_only=on;
DO $test$
BEGIN
  IF has_table_privilege('monitoring',(SELECT c.oid FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname='incident' AND c.relname='incidents'),'SELECT') THEN
    RAISE EXCEPTION 'monitoring can read raw incidents';
  END IF;
  IF (SELECT sum(value) FROM monitoring.business_metrics() WHERE metric='psim_incidents')<>2 THEN
    RAISE EXCEPTION 'incident aggregate differs';
  END IF;
  IF (SELECT sum(value) FROM monitoring.business_metrics() WHERE metric='psim_sites_without_duty')<>2 THEN
    RAISE EXCEPTION 'missing duty rows or fallback-only duty were omitted';
  END IF;
  IF (SELECT sum(value) FROM monitoring.business_metrics() WHERE metric='psim_connectors_online')<>1 OR
     (SELECT sum(value) FROM monitoring.business_metrics() WHERE metric='psim_connectors_offline')<>1 THEN
    RAISE EXCEPTION 'expired sessions were counted online';
  END IF;
  IF EXISTS(SELECT FROM monitoring.business_metrics() WHERE labels::text LIKE '%private%') THEN
    RAISE EXCEPTION 'private fields exported';
  END IF;
END
$test$;
ROLLBACK;
