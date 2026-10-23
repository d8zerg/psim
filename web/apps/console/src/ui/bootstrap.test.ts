import { afterEach, expect, it, vi } from "vitest";
import { startWithLocale } from "./bootstrap";
import * as dictionaries from "./locales/loader";
afterEach(() => vi.restoreAllMocks());
it("starts only after the selected dictionary loads and offers reload if it cannot load", async () => {
  const root = document.createElement("div");
  const render = vi.fn();
  await startWithLocale(root, render);
  expect(render).toHaveBeenCalledOnce();
  render.mockClear();
  vi.spyOn(dictionaries, "loadStrings").mockRejectedValueOnce(new Error("unavailable"));
  await startWithLocale(root, render, true);
  expect(render).not.toHaveBeenCalled();
  expect(root.querySelector('[role="alert"]')?.textContent).toContain("Could not load the interface");
  expect(root.querySelector("button")?.textContent).toContain("Retry");
});
