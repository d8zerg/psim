import { expect, it } from "vitest";
import { formatAbsolute, formatList, formatNumber, formatRelative, message, pseudolocalize, sortLabels } from "./intl";
it("formats ICU plurals in Russian and English rather than assembling translated fragments", () => {
  expect([1, 2, 5, 21].map((count) => message("ru", "incidents", { count }))).toEqual([
    "1 инцидент",
    "2 инцидента",
    "5 инцидентов",
    "21 инцидент",
  ]);
  expect(message("en", "incidents", { count: 1 })).toBe("1 incident");
  expect(message("en", "incidents", { count: 2 })).toBe("2 incidents");
  expect(message("ru", "minutes", { count: 2 })).toBe("2 минуты");
  expect(message("en", "minutes", { count: 1 })).toBe("1 minute");
  expect(message("ru", "importSummary", { total: 1, accepted: 1, rejected: 0 })).toBe(
    "1 строка: принято 1, отклонено 0",
  );
  expect(message("en", "importSummary", { total: 2, accepted: 1, rejected: 1 })).toBe("2 rows: 1 accepted, 1 rejected");
});
it("uses explicit time zones, deterministic relative time and locale-aware numbers, lists and sorting", () => {
  expect(formatNumber("ru", 1.5)).toBe("1,5");
  expect(formatNumber("en", 1.5, { minimumFractionDigits: 2 })).toBe("1.50");
  expect(formatAbsolute("en", 0, "UTC")).not.toBe(formatAbsolute("en", 0, "Europe/Moscow"));
  expect(() => formatAbsolute("en", 0, "invalid/zone")).toThrow(RangeError);
  expect(formatRelative("en", 0, 30000)).toBe("30 seconds ago");
  expect(formatRelative("en", 0, 120000)).toBe("2 minutes ago");
  expect(formatRelative("en", 0, 7200000)).toBe("2 hours ago");
  expect(formatRelative("en", 0, 86400000)).toBe("yesterday");
  expect(formatList("en", ["A", "B"])).toBe("A and B");
  expect(sortLabels("en", ["Device 10", "Device 2"])).toEqual(["Device 2", "Device 10"]);
  expect(pseudolocalize("Alarm тревога")).toBe("[AAlaarm треевоогаа]");
});
