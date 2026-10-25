import { readFileSync, writeFileSync } from "node:fs";
import { parse } from "yaml";
const contract = parse(
  readFileSync(new URL("../../../../contracts/openapi/psim-api-v1.yaml", import.meta.url), "utf8"),
  { maxAliasCount: 10000 },
);
const roots = [
  "psim.catalog.v1.Location",
  "psim.catalog.v1.Device",
  "psim.catalog.v1.DeviceType",
  "psim.catalog.v1.Connector",
  "psim.catalog.v1.MaintenanceWindow",
  "psim.processing.v1.MappingRuleSet",
  "psim.processing.v1.CorrelationRule",
  "psim.response.v1.ResponsePlan",
  "psim.response.v1.EscalationPolicy",
  "psim.incident.v1.IncidentType",
  "psim.config.v1.ProcessingSettings",
  "psim.config.v1.RetentionSettings",
];
const definitions = {};
const serverFields = new Set([
  "updatedAt",
  "createdAt",
  "createdBy",
  "publishedAt",
  "publishedBy",
  "activatedAt",
  "completedAt",
  "certificates",
  "floorPlan",
]);
function node(schema) {
  const ref = schema.$ref ?? schema.allOf?.[0]?.$ref;
  if (ref) {
    const name = ref.split("/").at(-1);
    if (!(name in definitions)) {
      definitions[name] = {};
      definitions[name] = node(contract.components.schemas[name]);
    }
    return { ref: name };
  }
  return {
    ...(schema.type ? { type: schema.type } : {}),
    ...(schema.enum ? { enum: schema.enum } : {}),
    ...(schema.format ? { format: schema.format } : {}),
    ...(schema.pattern ? { pattern: schema.pattern } : {}),
    ...(schema.properties
      ? {
          properties: Object.fromEntries(
            Object.entries(schema.properties)
              .filter(([key]) => !serverFields.has(key))
              .map(([key, value]) => [key, node(value)]),
          ),
        }
      : {}),
    ...(schema.items ? { items: node(schema.items) } : {}),
    ...(schema.additionalProperties ? { additionalProperties: node(schema.additionalProperties) } : {}),
  };
}
for (const ref of roots) node({ $ref: "#/components/schemas/" + ref });
writeFileSync(
  new URL("../src/gen/forms.ts", import.meta.url),
  "// Generated from the REST contract.\nexport const formSchemas = " + JSON.stringify(definitions) + ";\n",
);
console.log(`Administrative forms: ${Object.keys(definitions).length} contract schemas`);
