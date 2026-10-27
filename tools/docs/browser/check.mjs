import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { readFile, readdir, realpath, stat, mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

const require = createRequire('/opt/docs-browser/package.json');
const { chromium } = require('playwright');
const root = await realpath(process.argv[2]);
const output = process.argv[3];
const mime = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.woff2': 'font/woff2' };
const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://localhost');
    const target = await realpath(path.join(root, decodeURIComponent(url.pathname), url.pathname.endsWith('/') ? 'index.html' : ''));
    if (!target.startsWith(root + path.sep) || !(await stat(target)).isFile()) throw new Error('Invalid path');
    response.setHeader('Content-Type', mime[path.extname(target)] ?? 'application/octet-stream');
    response.end(await readFile(target));
  } catch {
    response.writeHead(404).end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({ headless: true });
const errors = [];
const external = [];
const checked = [];
try {
  await mkdir(output, { recursive: true });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  await context.route('**/*', async (route) => {
    const url = new URL(route.request().url());
    if (url.origin !== origin && !['data:', 'blob:'].includes(url.protocol)) {
      external.push(url.href);
      await route.abort();
    } else await route.continue();
  });
  const page = await context.newPage();
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('response', (response) => {
    if (response.status() >= 400 && !response.url().endsWith('/favicon.ico')) errors.push(`${response.status()} ${response.url()}`);
  });
  const routes = ['index.html', 'docs/engineering/installation-and-upgrades.html',
    'docs/engineering/connector-sdk.html', 'docs/architecture/runtime-view.html', 'reference/c4.html',
    'reference/kafka-topics.html', 'reference/error-codes.html', 'reference/rest-api.html',
    'contracts/docs/proto-reference.html'];
  for (const language of ['en', 'ru']) {
    const prefix = language === 'ru' ? 'ru/' : '';
    for (const route of routes) {
    const target = prefix + route;
    await page.goto(`${origin}/${target}`, { waitUntil: 'networkidle' });
    await page.locator('h1').first().waitFor();
    if (await page.locator('html').getAttribute('lang') !== language) throw new Error(`Wrong language: ${target}`);
    const brokenImages = await page.locator('img').evaluateAll((images) => images.filter((image) => !image.complete || image.naturalWidth === 0).map((image) => image.src));
    if (brokenImages.length) throw new Error(`Broken diagrams: ${brokenImages.join(', ')}`);
    if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)) throw new Error(`Page overflows: ${target}`);
    checked.push(target);
    if (route === 'reference/c4.html') await page.screenshot({ path: path.join(output, `c4-${language}.png`) });
    const alternate = page.locator('[data-language-switch]');
    const href = await alternate.getAttribute('href');
    const next = new URL(href, page.url());
    if (next.pathname !== '/' + (language === 'en' ? 'ru/' : '') + route) throw new Error(`Wrong counterpart: ${target}`);
    }
  await page.goto(`${origin}/${prefix}index.html`);
  await page.locator('.md-search__input').fill(language === 'en' ? 'connector' : 'коннектор');
  await page.waitForFunction(() => document.querySelectorAll('.md-search-result__link').length > 0);
  if (!await page.locator('.md-search-result__link').count()) throw new Error('Local search returned no results');
  const searchLinks = await page.locator('.md-search-result__link').evaluateAll((links) => links.map((link) => new URL(link.href).pathname));
  if (searchLinks.some((url) => url.startsWith('/ru/') !== (language === 'ru'))) throw new Error('Search crosses languages');
  await page.keyboard.press('Escape');
  await page.screenshot({ path: path.join(output, `portal-${language}.png`), animations: 'disabled' });
  await page.goto(`${origin}/${prefix}docs/engineering/installation-and-upgrades.html`, { waitUntil: 'networkidle' });
  const section = page.locator('article h2[id]').nth(1);
  const id = await section.getAttribute('id');
  const expected = await page.locator('#document-language-anchors').evaluate((node, anchor) => JSON.parse(node.textContent)[anchor], id);
  await section.evaluate((node) => { location.hash = node.id; node.scrollIntoView(); });
  await page.locator('[data-language-switch]').click();
  await page.waitForLoadState('networkidle');
  if (decodeURIComponent(new URL(page.url()).hash.slice(1)) !== expected) throw new Error('Language switch lost the section');
  const runbookDirectory = 'deploy/observability/runbooks/';
  const books = (await readdir(path.join(root, prefix, runbookDirectory))).filter((name) => name.endsWith('.html'));
  const search = JSON.parse(await readFile(path.join(root, prefix, 'search/search_index.json'), 'utf8'));
  if (books.length !== 32) throw new Error(`Missing runbooks: ${language}`);
  for (const book of books) {
    const target = prefix + runbookDirectory + book;
    await page.goto(`${origin}/${target}?from=now-1h&var-service=incident-service#diagnostics`);
    if (await page.locator('html').getAttribute('lang') !== language) throw new Error(`Wrong runbook language: ${target}`);
    if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)) throw new Error(`Runbook overflows: ${target}`);
    if (!search.docs.some((item) => item.location === runbookDirectory + book && item.text.length > 100)) throw new Error(`Runbook not searchable: ${target}`);
    await page.locator('[data-language-switch]').click();
    const next = new URL(page.url());
    const counterpart = '/' + (language === 'en' ? 'ru/' : '') + runbookDirectory + book;
    if (next.pathname !== counterpart || next.hash !== '#diagnostics' || next.searchParams.get('from') !== 'now-1h'
      || next.searchParams.get('var-service') !== 'incident-service') throw new Error(`Runbook switch lost context: ${target}`);
    checked.push(target);
  }
  await page.goto(`${origin}/${prefix}index.html`);
  await page.locator('.md-search__input').fill('PsimServiceUnavailable');
  await page.waitForFunction(() => [...document.querySelectorAll('.md-search-result__link')]
    .some((link) => link.href.includes('/runbooks/PsimServiceUnavailable.html')));
  }
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto(`${origin}/docs/engineering/connector-sdk.html`, { waitUntil: 'networkidle' });
  if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)) throw new Error('Mobile page overflows');
  await page.screenshot({ path: path.join(output, 'connector-mobile.png') });
  if (external.length || errors.length) throw new Error(JSON.stringify({ external: [...new Set(external)], errors: [...new Set(errors)] }));
  await writeFile(path.join(output, 'result.json'), JSON.stringify({ checked, search: ['en', 'ru'], sectionSwitch: true, mobile: true, externalRequests: external, browserErrors: errors }, null, 2) + '\n');
  console.log(`Documentation archive: ${checked.length} browser routes, local search, diagrams and mobile layout passed without network resources`);
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
