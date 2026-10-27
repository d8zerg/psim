import { createRequire } from 'node:module';
import { readFile, mkdir } from 'node:fs/promises';
import path from 'node:path';

const require = createRequire('/repo/web/apps/console/package.json');
const { chromium } = require('@playwright/test');
const directory = process.argv[2];
const password = (await readFile(path.join(directory, 'runtime/material/psim-obs/grafana/admin.secret'), 'utf8')).trim();
const browser = await chromium.launch({ headless: true });
try {
  const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport: { width: 1440, height: 1000 }, locale: 'ru-RU' });
  const page = await context.newPage();
  const failures = [];
  page.on('requestfailed', (request) => {
    if (/\/api\/ds\/query|\/api\/datasources\//.test(request.url())) failures.push(request.failure()?.errorText);
  });
  page.on('response', async (response) => {
    if (response.url().includes('/api/datasources/') && response.status() >= 400) failures.push(response.status());
    if (response.url().includes('/api/ds/query')) {
      if (response.status() >= 400) failures.push(response.status());
      try {
        const body = await response.json();
        for (const value of Object.values(body.results ?? {})) if (value.error) failures.push(value.error);
      } catch {}
    }
  });
  await page.goto('https://127.0.0.1:19444/monitoring/login');
  await page.locator('input[name="user"]').fill('recovery-admin');
  await page.locator('input[name="password"]').fill(password);
  await page.locator('button[type="submit"]').click();
  await page.waitForURL((url) => !url.pathname.endsWith('/login'));
  await mkdir('/repo/build/observability/screenshots', { recursive: true });
  for (const language of ['en', 'ru']) {
  for (const key of ['overview', 'pipeline', 'services', 'infrastructure', 'storage', 'business', 'operations']) {
    const suffix = language === 'ru' ? '-ru' : '';
    const alertTitle = language === 'ru' ? 'Активные алерты' : 'Active Alerts';
    await page.goto('https://127.0.0.1:19444/monitoring/d/psim-' + key + suffix + '?from=now-1h&to=now');
    await page.getByText(alertTitle, { exact: true }).waitFor();
    await page.waitForLoadState('networkidle');
    const counterpart = page.getByRole('link', { name: language === 'en' ? 'RU' : 'EN', exact: true });
    const translated = new URL(await counterpart.getAttribute('href'), page.url());
    const current = new URL(page.url());
    if (translated.pathname !== '/monitoring/d/psim-' + key + (language === 'en' ? '-ru' : '')) {
      throw new Error('Wrong dashboard language counterpart: ' + key);
    }
    for (const [name, value] of current.searchParams) {
      if ((name === 'from' || name === 'to' || name.startsWith('var-')) && !translated.searchParams.getAll(name).includes(value)) {
        throw new Error('Dashboard language switch lost context: ' + name);
      }
    }
    const errors = await page.locator('[aria-label="Panel error"]').allTextContents();
    failures.push(...errors);
    await page.screenshot({ path: '/repo/build/observability/screenshots/' + key + '-' + language + '.png' });
    for (const panel of await page.locator('[data-testid^="data-testid Panel header "]').all()) {
      await panel.scrollIntoViewIfNeeded();
      await page.waitForLoadState('networkidle');
    }
    const alerts = page.getByTestId('data-testid Panel header ' + alertTitle);
    await alerts.getByText('PsimServiceUnavailable', { exact: true }).first().waitFor();
    await alerts.getByText(language === 'ru' ? 'Уровень' : 'Severity', { exact: true }).waitFor();
    await alerts.getByText(language === 'ru' ? 'Контекст' : 'Context', { exact: true }).waitFor();
    if (await alerts.getByRole('columnheader').count() !== 5) throw new Error('Unexpected alert table columns: ' + key);
    await page.screenshot({ path: '/repo/build/observability/screenshots/' + key + '-' + language + '-details.png' });
    const links = await alerts.locator('a').evaluateAll((nodes) => nodes.map((node) => ({ text: node.textContent, href: node.href })));
    if (!links.some((link) => new URL(link.href).pathname === '/operations/runbooks/PsimServiceUnavailable' + (language === 'ru' ? 'Ru' : ''))) {
      throw new Error('Alert runbook link unavailable: ' + JSON.stringify(links));
    }
    if (await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)) {
      throw new Error('Dashboard exceeds viewport: ' + key);
    }
  }
  }
  if (failures.length) throw new Error('Dashboard query failures: ' + failures.join(', '));
  console.log('Fourteen EN/RU Grafana dashboards rendered at 1440px without query errors or horizontal page overflow');
} finally {
  await browser.close();
}
