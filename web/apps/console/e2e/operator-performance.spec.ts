import { mkdir, writeFile } from "node:fs/promises";
import { expect, test } from "@playwright/test";
import { operatorFixture } from "./operator-fixture";
test("operator feed scrolls 10000 incidents at 50 frames per second and renders realtime arrivals within one second p99", async ({
  page,
}) => {
  const f = await operatorFixture(page, 10000);
  await expect(page.getByTestId("incident-row").first()).toBeVisible();
  expect(await page.getByTestId("incident-row").count()).toBeLessThan(20);
  const scroll = page.locator(".operator-feed-scroll");
  await scroll.evaluate((node) => {
    node.scrollTop = 50000;
  });
  await page.waitForFunction(
    () => Number(document.querySelector<HTMLElement>("[data-incident-id]")?.style.top.replace("px", "")) > 0,
  );
  const frames = await scroll.evaluate(async (node) => {
    const stamps: number[] = [];
    const started = performance.now();
    await new Promise<void>((resolve) => {
      const tick = (at: number) => {
        stamps.push(at);
        node.scrollTop += 160;
        if (at - started >= 2500) resolve();
        else requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    });
    return { fps: ((stamps.length - 1) * 1000) / ((stamps.at(-1) ?? 0) - (stamps[0] ?? 0)), frames: stamps.length };
  });
  expect(frames.fps).toBeGreaterThanOrEqual(50);
  await scroll.evaluate((node) => {
    node.scrollTop = 0;
  });
  const latency: number[] = [];
  for (let i = 1; i <= 30; i++) {
    const start = await page.evaluate(() => performance.timeOrigin + performance.now());
    f.deliver(i);
    const row = page
      .getByTestId("incident-row")
      .filter({ hasText: `Arrival ${String(i)}` })
      .first();
    await expect(row).toBeVisible();
    const rendered = Number(await row.getAttribute("data-rendered-at"));
    latency.push(rendered - start);
  }
  const sorted = [...latency].sort((a, b) => a - b);
  const p99 = sorted[Math.ceil(sorted.length * 0.99) - 1] ?? Infinity;
  expect(p99).toBeLessThanOrEqual(1000);
  await mkdir("/repo/build/operator", { recursive: true });
  await writeFile(
    "/repo/build/operator/performance.json",
    JSON.stringify({ incidents: 10000, ...frames, samples: latency, p99 }, null, 2) + "\n",
  );
});
