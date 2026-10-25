import type { paths } from "@psim/api-client";
import { formSchemas } from "@psim/api-client/form-schema";
export type Value = string | number | boolean | null | Value[] | { [key: string]: Value };
export type RecordValue = Record<string, Value>;
export interface Node {
  readonly ref?: string;
  readonly type?: string;
  readonly enum?: readonly string[];
  readonly format?: string;
  readonly pattern?: string;
  readonly properties?: Readonly<Record<string, Node>>;
  readonly items?: Node;
  readonly additionalProperties?: Node;
}
export const schemas: Readonly<Record<string, Node>> = formSchemas;
export function resolve(node: Node): Node {
  return node.ref ? (schemas[node.ref] ?? {}) : node;
}
export function record(value: Value | undefined): RecordValue {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
}
export function string(value: Value | undefined): string {
  return typeof value === "string" || typeof value === "number" ? String(value) : "";
}
export function list(value: Value | undefined): Value[] {
  return Array.isArray(value) ? value : [];
}
export const groups: Readonly<Record<string, readonly string[]>> = {
  "psim.processing.v1.CorrelationRule": ["single", "conjunction", "sequence", "threshold", "absence"],
  "psim.response.v1.Step": [
    "instruction",
    "checklist",
    "decision",
    "command",
    "notify",
    "wait",
    "escalate",
    "automated",
  ],
  "psim.response.v1.Recipient": ["userId", "role", "currentDuty"],
  "psim.response.v1.CommandParams": ["deviceId", "targetExpression"],
};
export interface Resource {
  readonly path: keyof paths;
  readonly schema: string;
  readonly collection: string;
  readonly item: string;
  readonly id: string;
  readonly immutable?: boolean;
  readonly singleton?: boolean;
  readonly integration?: boolean;
  readonly global?: boolean;
}
export const resources = {
  locations: {
    path: "/v1/locations",
    schema: "psim.catalog.v1.Location",
    collection: "locations",
    item: "location",
    id: "locationId",
  },
  devices: {
    path: "/v1/devices",
    schema: "psim.catalog.v1.Device",
    collection: "devices",
    item: "device",
    id: "deviceId",
  },
  deviceTypes: {
    path: "/v1/device-types",
    schema: "psim.catalog.v1.DeviceType",
    collection: "deviceTypes",
    item: "deviceType",
    id: "deviceTypeId",
    global: true,
  },
  connectors: {
    path: "/v1/connectors",
    schema: "psim.catalog.v1.Connector",
    collection: "connectors",
    item: "connector",
    id: "connectorId",
    integration: true,
    global: true,
  },
  mappings: {
    path: "/v1/mappings",
    schema: "psim.processing.v1.MappingRuleSet",
    collection: "mappingRuleSets",
    item: "mappingRuleSet",
    id: "connectorType",
    immutable: true,
    integration: true,
    global: true,
  },
  incidentTypes: {
    path: "/v1/incident-types",
    schema: "psim.incident.v1.IncidentType",
    collection: "incidentTypes",
    item: "incidentType",
    id: "incidentTypeId",
    global: true,
  },
  policies: {
    path: "/v1/escalation-policies",
    schema: "psim.response.v1.EscalationPolicy",
    collection: "policies",
    item: "policy",
    id: "policyId",
    global: true,
  },
  rules: {
    path: "/v1/rules",
    schema: "psim.processing.v1.CorrelationRule",
    collection: "rules",
    item: "rule",
    id: "ruleId",
    immutable: true,
    global: true,
  },
  plans: {
    path: "/v1/plans",
    schema: "psim.response.v1.ResponsePlan",
    collection: "plans",
    item: "plan",
    id: "planId",
    immutable: true,
    global: true,
  },
  maintenance: {
    path: "/v1/maintenance-windows",
    schema: "psim.catalog.v1.MaintenanceWindow",
    collection: "maintenanceWindows",
    item: "maintenanceWindow",
    id: "maintenanceId",
  },
  processing: {
    path: "/v1/settings",
    schema: "psim.config.v1.ProcessingSettings",
    collection: "",
    item: "processing",
    id: "",
    singleton: true,
    global: true,
  },
  retention: {
    path: "/v1/settings",
    schema: "psim.config.v1.RetentionSettings",
    collection: "",
    item: "retention",
    id: "",
    singleton: true,
    global: true,
  },
} satisfies Record<string, Resource>;
export type ResourceKey = keyof typeof resources;
export function newRecord(key: ResourceKey): RecordValue {
  const resource: Resource = resources[key];
  const id = crypto.randomUUID();
  const common: RecordValue = resource.id ? { [resource.id]: id } : {};
  switch (key) {
    case "locations":
      return { ...common, kind: "LOCATION_KIND_SITE", timezone: "UTC", state: "RESOURCE_STATE_ACTIVE" };
    case "devices":
    case "deviceTypes":
    case "connectors":
      return { ...common, state: "RESOURCE_STATE_ACTIVE" };
    case "mappings":
      return { mappingRuleSetId: id, connectorType: "", taxonomyVersion: "1", rules: [] };
    case "rules":
      return {
        ...common,
        state: "RULE_STATE_DRAFT",
        filter: { types: [] },
        correlationScope: "CORRELATION_SCOPE_ZONE",
        single: {},
        output: { priority: "PRIORITY_P2" },
        window: "60s",
        allowedLateness: "5s",
        suppression: "0s",
      };
    case "plans":
      return {
        ...common,
        state: "PLAN_STATE_DRAFT",
        startStepId: "start",
        bindings: [],
        steps: [{ stepId: "start", title: "", instruction: { text: "" } }],
      };
    case "incidentTypes":
      return { ...common, defaultPriority: "PRIORITY_P2", groupingWindow: "60s", autoCloseAfter: "0s" };
    case "policies":
      return { ...common, sla: [], levels: [] };
    case "maintenance":
      return { ...common, suppression: { eventTypes: [], ruleIds: [] } };
    case "processing":
    case "retention":
      return {};
  }
}
export function initial(node: Node): Value {
  const schema = resolve(node);
  if (schema.enum) return schema.enum.find((value) => !value.endsWith("_UNSPECIFIED")) ?? "";
  if (schema.type === "boolean") return false;
  if (schema.type === "number" || schema.type === "integer") return 0;
  if (schema.type === "array") return [];
  if (schema.type === "object") return {};
  return "";
}
export function editable(key: ResourceKey, value: RecordValue): RecordValue {
  const omitted = [
    "updatedAt",
    "createdAt",
    "createdBy",
    "publishedAt",
    "publishedBy",
    "activatedAt",
    "completedAt",
    "certificates",
    "floorPlan",
    ...(key === "maintenance" ? ["state"] : []),
  ];
  return Object.fromEntries(Object.entries(value).filter(([field]) => !omitted.includes(field)));
}
