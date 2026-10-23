import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import * as dictionaries from "./locales/loader";
import { defaults, setPreferences, usePreferences } from "./preferences";
import { DisplaySettings } from "./settings";
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  setPreferences(defaults);
});
it("keeps the active language during loading and failure and ignores superseded responses", async () => {
  setPreferences({ ...defaults, language: "en" });
  let resolve: (() => void) | undefined;
  const delayed = new Promise<void>((done) => {
    resolve = done;
  });
  const load = vi.spyOn(dictionaries, "loadStrings").mockReturnValueOnce(delayed);
  render(<DisplaySettings design />);
  const language = screen.getByLabelText("Language");
  language.focus();
  fireEvent.change(language, { target: { value: "ru" } });
  expect(usePreferences.getState().language).toBe("en");
  expect(language.getAttribute("aria-busy")).toBe("true");
  fireEvent.change(language, { target: { value: "en" } });
  await act(async () => {
    await Promise.resolve();
  });
  await act(async () => {
    resolve?.();
    await delayed;
  });
  expect(usePreferences.getState().language).toBe("en");
  expect(document.activeElement).toBe(language);
  load.mockRejectedValueOnce(new Error("unavailable"));
  fireEvent.change(language, { target: { value: "ru" } });
  expect((await screen.findByRole("alert")).textContent).toContain("current screen remains available");
  expect(usePreferences.getState().language).toBe("en");
  expect(language.getAttribute("aria-busy")).toBe("false");
});
