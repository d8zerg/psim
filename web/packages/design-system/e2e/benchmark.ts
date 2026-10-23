import { mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { expect, test } from "@playwright/test";
test("@benchmark compares non-virtual and virtual rendering on identical seeded data", async ({ page }) => {
  test.setTimeout(120000);
  const results: Record<
    string,
    {
      updateMedian: number;
      updateP95: number;
      inputMedian: number;
      inputP95: number;
      rows: number;
      unchangedRemounts: number;
    }
  > = {};
  const series: Record<string, { updates: number[]; inputs: number[] }> = {};
  for (const renderer of ["baseline", "virtual"]) {
    await page.goto(`/?story=data--states&mode=preview&benchmark=true&renderer=${renderer}&psim-language=en`);
    await expect(page.getByTestId("benchmark")).toBeVisible();
    await page.evaluate(() => document.fonts.ready);
    const measurements = await page.evaluate(async () => {
      const update = Array.from(document.querySelectorAll("button")).find(
        (element) => element.textContent === "Update batch",
      );
      const label = Array.from(document.querySelectorAll("label")).find(
        (element) => element.textContent === "Search benchmark",
      );
      const input = label?.htmlFor ? document.getElementById(label.htmlFor) : null;
      if (!update || !(input instanceof HTMLInputElement)) throw new Error("Benchmark controls are required");
      const setValue = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value")?.set?.bind(input);
      if (!setValue) throw new Error("Native input setter is required");
      const settle = () =>
        new Promise<void>((resolve) => {
          requestAnimationFrame(() => {
            resolve();
          });
        });
      async function measure(action: () => void, target: Element, attribute: string) {
        await settle();
        return new Promise<number>((resolve, reject) => {
          const start = performance.now();
          const observer = new MutationObserver(() => {
            observer.disconnect();
            window.clearTimeout(timeout);
            resolve(performance.now() - start);
          });
          const timeout = window.setTimeout(() => {
            observer.disconnect();
            reject(new Error(`No committed ${attribute} update`));
          }, 2000);
          observer.observe(target, { attributes: true, attributeFilter: [attribute] });
          action();
        });
      }
      const fixture = document.querySelector('[data-testid="benchmark"]');
      if (!fixture) throw new Error("Benchmark fixture is required");
      const updates: number[] = [];
      const inputs: number[] = [];
      let unchangedRemounts = 0;
      for (let index = 0; index < 25; index++) {
        const retained = fixture.querySelector('[data-id="ROW-00001"]');
        if (!retained) throw new Error("The unchanged record must be mounted");
        const updateDuration = await measure(
          () => {
            update.click();
          },
          fixture,
          "data-revision",
        );
        if (index >= 5) updates.push(updateDuration);
        if (retained !== fixture.querySelector('[data-id="ROW-00001"]')) unchangedRemounts++;
        const inputDuration = await measure(
          () => {
            setValue(index % 2 ? "S" : "SE");
            input.dispatchEvent(new Event("input", { bubbles: true }));
          },
          input,
          "value",
        );
        if (index >= 5) inputs.push(inputDuration);
      }
      return {
        updates,
        inputs,
        unchangedRemounts,
        rows: document.querySelectorAll('[data-testid="benchmark"] tbody tr').length,
      };
    });
    function percentile(samples: number[], fraction: number) {
      const sorted = [...samples].sort((a, b) => a - b);
      const value = sorted[Math.ceil(sorted.length * fraction) - 1];
      if (value === undefined) throw new Error("Empty benchmark series");
      return Number(value.toFixed(3));
    }
    series[renderer] = { updates: measurements.updates, inputs: measurements.inputs };
    results[renderer] = {
      updateMedian: percentile(measurements.updates, 0.5),
      updateP95: percentile(measurements.updates, 0.95),
      inputMedian: percentile(measurements.inputs, 0.5),
      inputP95: percentile(measurements.inputs, 0.95),
      rows: measurements.rows,
      unchangedRemounts: measurements.unchangedRemounts,
    };
  }
  const baseline = results.baseline;
  const virtual = results.virtual;
  if (!baseline || !virtual) throw new Error("Both renderers are required");
  expect(baseline.rows).toBe(5000);
  expect(virtual.rows).toBeLessThan(20);
  expect(baseline.unchangedRemounts).toBe(0);
  expect(virtual.unchangedRemounts).toBe(0);
  expect(virtual.updateMedian).toBeLessThan(baseline.updateMedian);
  expect(virtual.inputMedian).toBeLessThan(baseline.inputMedian);
  expect(virtual.updateP95).toBeLessThan(baseline.updateP95);
  expect(virtual.inputP95).toBeLessThan(baseline.inputP95);
  const directory = resolve(import.meta.dirname, "../../../../build/web-benchmark");
  mkdirSync(directory, { recursive: true });
  writeFileSync(
    resolve(directory, "results.json"),
    JSON.stringify(
      {
        browser: "Chromium",
        viewport: "1280x1000",
        warmup: 5,
        samples: 20,
        records: 5000,
        changedPerBatch: 500,
        series,
        results,
      },
      null,
      2,
    ) + "\n",
  );
  console.log(JSON.stringify(results));
});
