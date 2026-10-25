import { SelectField } from "@psim/design-system";
import type { ComponentProps } from "react";
export function Select({
  options,
  ...props
}: ComponentProps<typeof SelectField> & {
  readonly options: readonly { readonly value: string; readonly label: string }[];
}) {
  return (
    <SelectField {...props}>
      {options.map((option) => (
        <option key={option.value} value={option.value}>
          {option.label}
        </option>
      ))}
    </SelectField>
  );
}
