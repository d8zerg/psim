import { Button, Field, SelectField } from "@psim/design-system";
import { groups, initial, list, record, resolve, string, type Node, type Value, type RecordValue } from "./model";
import { enumLabel, label, useAdminStrings } from "./strings";
interface Props {
  readonly node: Node;
  readonly value: Value | undefined;
  readonly change: (value: Value | undefined) => void;
  readonly name: string;
  readonly omit?: readonly string[];
  readonly locked?: readonly string[];
}
/** Nested contract objects use native controls; the selected oneof replaces every sibling variant. */
export function FormFields({ node, value, change, name, omit = [], locked = [] }: Props) {
  const { t, language } = useAdminStrings();
  const schema = resolve(node);
  if (schema.properties) {
    const object = record(value);
    const variants = node.ref ? (groups[node.ref] ?? []) : [];
    const selected = variants.find((key) => object[key] !== undefined) ?? "";
    const set = (key: string, next: Value | undefined) => {
      let result: RecordValue = { ...object };
      if (node.ref === "psim.catalog.v1.Location" && key === "kind") {
        result = Object.fromEntries(
          Object.entries(result).filter(
            ([field]) => field !== (next === "LOCATION_KIND_SITE" ? "parentId" : "timezone"),
          ),
        );
        if (next === "LOCATION_KIND_SITE") result.timezone = "UTC";
      }
      if (next !== undefined) result[key] = next;
      change(
        next === undefined ? Object.fromEntries(Object.entries(result).filter(([field]) => field !== key)) : result,
      );
    };
    return (
      <div className="admin-fields">
        {variants.length > 0 && (
          <SelectField
            label={`${name}: ${t.variant}`}
            value={selected}
            onChange={(event) => {
              const next = Object.fromEntries(Object.entries(object).filter(([field]) => !variants.includes(field)));
              const key = event.target.value;
              if (key) next[key] = key === "currentDuty" ? true : initial(schema.properties?.[key] ?? {});
              change(next);
            }}
          >
            <option value="">{t.select}</option>
            {variants.map((key) => (
              <option key={key} value={key}>
                {label(key, language)}
              </option>
            ))}
          </SelectField>
        )}
        {Object.entries(schema.properties)
          .filter(([key]) => !omit.includes(key) && (!variants.includes(key) || key === selected))
          .map(([key, child]) => {
            const caption = label(key, language);
            const nested = resolve(child);
            const complex = nested.type === "object" || nested.type === "array";
            return (
              <fieldset key={key} disabled={locked.includes(key)} className="admin-field">
                {complex ? (
                  <>
                    <legend>{caption}</legend>
                    {object[key] === undefined ? (
                      <Button
                        type="button"
                        onClick={() => {
                          set(key, initial(child));
                        }}
                      >
                        {t.enable}: {caption}
                      </Button>
                    ) : (
                      <>
                        <FormFields
                          node={child}
                          value={object[key]}
                          name={caption}
                          change={(next) => {
                            set(key, next);
                          }}
                        />
                        {!variants.includes(key) && (
                          <Button
                            type="button"
                            variant="ghost"
                            onClick={() => {
                              set(key, undefined);
                            }}
                          >
                            {t.remove}: {caption}
                          </Button>
                        )}
                      </>
                    )}
                  </>
                ) : (
                  <FormFields
                    node={child}
                    value={object[key]}
                    name={caption}
                    change={(next) => {
                      set(key, next);
                    }}
                  />
                )}
              </fieldset>
            );
          })}
      </div>
    );
  }
  if (schema.type === "array" && schema.items) {
    const values = list(value);
    const item = schema.items;
    return (
      <div className="admin-fields">
        {values.map((entry, index) => (
          <fieldset key={index} className="admin-entry">
            <legend>
              {name} {index + 1}
            </legend>
            <FormFields
              node={item}
              value={entry}
              name={`${name} ${String(index + 1)}`}
              change={(next) => {
                change(values.map((current, i) => (i === index ? (next ?? initial(item)) : current)));
              }}
            />
            <Button
              type="button"
              variant="ghost"
              onClick={() => {
                change(values.filter((_, i) => i !== index));
              }}
            >
              {t.remove}: {name} {index + 1}
            </Button>
          </fieldset>
        ))}
        <Button
          type="button"
          onClick={() => {
            change([...values, initial(item)]);
          }}
        >
          {t.add}: {name}
        </Button>
      </div>
    );
  }
  if (schema.additionalProperties) {
    const object = record(value);
    const child = schema.additionalProperties;
    return (
      <div className="admin-fields">
        {Object.entries(object).map(([key, entry], index) => (
          <fieldset key={index} className="admin-entry">
            <legend>
              {name} {index + 1}
            </legend>
            <Field
              label={`${name}: ${t.id} ${String(index + 1)}`}
              defaultValue={key}
              onBlur={(event) => {
                const next = event.target.value.trim();
                if (!next || (next !== key && next in object)) {
                  event.target.value = key;
                  return;
                }
                change(
                  Object.fromEntries(Object.entries(object).map(([field, val]) => [field === key ? next : field, val])),
                );
              }}
            />
            <FormFields
              node={child}
              value={entry}
              name={`${name} ${String(index + 1)}`}
              change={(next) => {
                change({ ...object, [key]: next ?? "" });
              }}
            />
            <Button
              type="button"
              onClick={() => {
                change(Object.fromEntries(Object.entries(object).filter(([field]) => field !== key)));
              }}
            >
              {t.remove}: {name} {index + 1}
            </Button>
          </fieldset>
        ))}
        <Button
          type="button"
          onClick={() => {
            let key = "key";
            while (key in object) key += "_";
            change({ ...object, [key]: initial(child) });
          }}
        >
          {t.add}: {name}
        </Button>
      </div>
    );
  }
  if (schema.enum)
    return (
      <SelectField
        label={name}
        value={string(value)}
        onChange={(event) => {
          change(event.target.value || undefined);
        }}
      >
        <option value="">{t.select}</option>
        {schema.enum
          .filter((option) => !option.endsWith("_UNSPECIFIED"))
          .map((option) => (
            <option key={option} value={option}>
              {enumLabel(option, language)}
            </option>
          ))}
      </SelectField>
    );
  if (schema.type === "boolean")
    return (
      <label className="admin-checkbox">
        <input
          type="checkbox"
          checked={value === true}
          onChange={(event) => {
            change(event.target.checked);
          }}
        />
        {name}
      </label>
    );
  const numeric = schema.type === "integer" || schema.type === "number";
  return (
    <Field
      label={name}
      type={numeric ? "number" : "text"}
      step={schema.type === "integer" ? 1 : "any"}
      value={string(value)}
      hint={schema.pattern ? t.duration : schema.format === "date-time" ? t.utc : undefined}
      onChange={(event) => {
        change(event.target.value === "" ? undefined : numeric ? Number(event.target.value) : event.target.value);
      }}
    />
  );
}
