import { expect, it } from "vitest";
import { IncidentState, Priority } from "@psim/realtime-client";
import { protoLabels } from "./proto-labels";
it("provides complete nonempty labels for all displayed contract enum values in both languages", () => {
  for (const language of ["ru", "en"] as const) {
    for (const enumLabels of Object.values(protoLabels[language])) {
      expect(Object.values(enumLabels).every((label) => label.trim().length > 0)).toBe(true);
    }
    expect(Object.keys(protoLabels[language].incident)).toHaveLength(
      Object.values(IncidentState).filter((v) => typeof v === "number").length,
    );
    expect(Object.keys(protoLabels[language].priority)).toHaveLength(
      Object.values(Priority).filter((v) => typeof v === "number").length,
    );
  }
  expect(protoLabels.en.command[10]).toBe("Outcome unknown");
  expect(protoLabels.ru.command[5]).toBe("Выполнена");
});
