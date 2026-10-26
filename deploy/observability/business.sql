CREATE SCHEMA IF NOT EXISTS monitoring;
REVOKE ALL ON SCHEMA monitoring FROM PUBLIC;
GRANT USAGE ON SCHEMA monitoring TO monitoring;
CREATE OR REPLACE FUNCTION monitoring.business_metrics()
RETURNS TABLE(metric text, labels jsonb, value double precision)
LANGUAGE plpgsql SECURITY DEFINER SET search_path=pg_catalog SET statement_timeout='3s'
AS $function$
BEGIN
  IF to_regclass('incident.incidents') IS NOT NULL THEN
    RETURN QUERY EXECUTE $query$
      SELECT 'psim_incidents'::text,
             jsonb_build_object('tenant',tenant_id::text,'state',COALESCE(body->>'state','unknown')),
             count(*)::double precision FROM incident.incidents GROUP BY tenant_id,body->>'state'
    $query$;
  END IF;
  IF to_regclass('ingest.registrations') IS NOT NULL AND to_regclass('ingest.sessions') IS NOT NULL THEN
    RETURN QUERY EXECUTE $query$
      SELECT 'psim_connectors_online'::text,jsonb_build_object('tenant',r.tenant_id::text),
             count(*) FILTER(WHERE s.online AND s.expires_at>now())::double precision
      FROM ingest.registrations r LEFT JOIN ingest.sessions s USING(connector_id)
      WHERE r.body->>'state'='RESOURCE_STATE_ACTIVE' GROUP BY r.tenant_id
    $query$;
    RETURN QUERY EXECUTE $query$
      SELECT 'psim_connectors_offline'::text,jsonb_build_object('tenant',r.tenant_id::text),
             count(*) FILTER(WHERE NOT COALESCE(s.online AND s.expires_at>now(),false))::double precision
      FROM ingest.registrations r LEFT JOIN ingest.sessions s USING(connector_id)
      WHERE r.body->>'state'='RESOURCE_STATE_ACTIVE' GROUP BY r.tenant_id
    $query$;
  END IF;
  IF to_regclass('catalog.resources') IS NOT NULL AND to_regclass('escalation.duties') IS NOT NULL THEN
    RETURN QUERY EXECUTE $query$
      SELECT 'psim_sites_without_duty'::text,jsonb_build_object('tenant',r.tenant::text),
             count(*) FILTER(WHERE COALESCE(d.body->>'userId','')='')::double precision
      FROM catalog.resources r LEFT JOIN escalation.duties d ON d.tenant=r.tenant AND d.site=r.id
      WHERE r.kind='location' AND r.body->>'kind'='LOCATION_KIND_SITE'
        AND COALESCE(r.body->>'state','')<>'RESOURCE_STATE_ARCHIVED' GROUP BY r.tenant
    $query$;
  END IF;
END
$function$;
REVOKE ALL ON FUNCTION monitoring.business_metrics() FROM PUBLIC;
GRANT EXECUTE ON FUNCTION monitoring.business_metrics() TO monitoring;
