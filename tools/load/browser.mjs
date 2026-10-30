import { chromium } from '/repo/web/apps/console/node_modules/@playwright/test/index.mjs';
import { readFile, writeFile, rename, access } from 'node:fs/promises';
import { createHmac } from 'node:crypto';

const fixture = JSON.parse(await readFile('/case/load-browser.json', 'utf8'));
const consoles = Number(process.env.PSIM_LOAD_CONSOLES ?? 500);
if (!Number.isInteger(consoles) || consoles < 1 || consoles > 500) throw new Error('invalid console count');
const browser = await chromium.launch({ headless: true });
const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport: { width: 1440, height: 1000 } });
const page = await context.newPage();
const renders = {};
const errors = [];
const incidentDeliveries = [];
let feedSocket;
page.on('websocket', socket => {
  if (!socket.url().endsWith('/v1/realtime')) return;
  if (feedSocket && !feedSocket.isClosed()) return;
  feedSocket = socket;
  socket.on('framereceived', frame => {
    try {
      const value = JSON.parse(String(frame.payload));
      const event = value.delta?.incidentEvent;
      // Automation observes the frame asynchronously; DOM timestamps measure rendering independently.
      if (event) incidentDeliveries.push({ observed_ms: Date.now(), event });
    } catch { /* Ignore transport frames that contain no JSON event. */ }
  });
});
page.on('pageerror', error => errors.push(error.message));
let accessToken = '';
page.on('response', async response => {
  if (response.url().endsWith('/protocol/openid-connect/token') && response.ok()) {
    const body = await response.json();
    accessToken = body.access_token;
  }
});
try {
  await page.exposeFunction('loadRender', (id, version, at) => { renders[id + ':' + version] ??= at; });
  await page.addInitScript(() => {
    localStorage.setItem('psim.ui', JSON.stringify({ language: 'en' }));
    new MutationObserver(() => {
      document.querySelectorAll('[data-event-id][data-rendered-at]').forEach(row => {
        if (row.dataset.eventId) void window.loadRender(row.dataset.eventId, row.dataset.version, Number(row.dataset.renderedAt));
      });
    }).observe(document, { subtree: true, attributes: true, childList: true });
  });
  await page.goto(fixture.origin);
  await page.getByRole('button', { name: 'Sign in', exact: true }).click();
  await page.locator('#username').fill(fixture.username);
  await page.locator('#password').fill(fixture.password);
  await page.locator('#kc-login').click();
  await page.getByRole('link', { name: 'Unable to scan?' }).click();
  const secret = (await page.locator('#kc-totp-secret-key, #totp-secret').textContent()).replaceAll(/\s/gu, '');
  const bits = [...secret.toUpperCase()].map(char => 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567'.indexOf(char).toString(2).padStart(5, '0')).join('');
  const key = Buffer.from((bits.match(/.{8}/gu) ?? []).map(byte => Number.parseInt(byte, 2)));
  const counter = Buffer.alloc(8);
  counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
  const mac = createHmac('sha1', key).update(counter).digest();
  const code = String((mac.readUInt32BE(mac.at(-1) & 15) & 0x7fffffff) % 1000000).padStart(6, '0');
  await page.locator('input[name=totp]').fill(code);
  await page.locator('input[name=userLabel]').fill('Load qualification');
  await page.getByRole('button', { name: 'Submit', exact: true }).click();
  await page.locator('[data-testid="connection-status"][data-status="open"]').waitFor({ timeout: 60000 });
  await page.evaluate(async ({ token, consoles }) => {
    window.loadSockets = [];
    window.loadRealtime = [];
    window.loadSocketErrors = [];
    const snapshot = await fetch('/v1/feed/incidents', { headers: { Authorization: 'Bearer ' + token } }).then(response => {
      if (!response.ok) throw new Error('snapshot failed: ' + response.status);
      return response.json();
    });
    for (let batch = 0; batch < consoles - 1; batch += 16) {
      await Promise.all(Array.from({ length: Math.min(16, consoles - 1 - batch) }, (_, offset) => new Promise((resolve, reject) => {
        const socket = new WebSocket(location.origin.replace('https:', 'wss:') + '/v1/realtime');
        const deadline = setTimeout(() => reject(new Error('subscription timeout')), 30000);
        let resumeToken = snapshot.resumeToken;
        let request = 0;
        socket.loadSend = frame => socket.send(JSON.stringify({ requestId: String(++request), ...frame }));
        socket.onopen = () => socket.loadSend({ authenticate: { accessToken: token } });
        socket.onerror = () => { window.loadSocketErrors.push('transport'); reject(new Error('websocket error')); };
        socket.onclose = event => { if (!window.loadStopping) window.loadSocketErrors.push('closed ' + event.code); };
        socket.onmessage = event => {
          const value = JSON.parse(event.data);
          if (value.authenticated) socket.loadSend({ subscribe: {
            subscriptionId: 'load', stream: 'STREAM_INCIDENTS', resumeToken, filter: {},
          } });
          if (value.subscribed) { resumeToken = value.subscribed.resumeToken || resumeToken; clearTimeout(deadline); resolve(); }
          if (value.delta) resumeToken = value.delta.resumeToken || resumeToken;
          if (value.delta?.incidentEvent) {
            const envelope = value.delta.incidentEvent.envelope;
            window.loadRealtime.push({ client: batch + offset, event: envelope.messageId, milliseconds: Date.now() - Date.parse(envelope.producedAt) });
          }
          if (value.error) window.loadSocketErrors.push(JSON.stringify(value.error));
          if (value.resyncRequired) window.loadSocketErrors.push('resync required');
        };
        window.loadSockets.push(socket);
      })));
    }
  }, { token: accessToken, consoles });
  console.log('READY');
  let previousToken = accessToken;
  let lastPing = Date.now();
  let lastProgress = 0;
  for (;;) {
    try { await access('/case/load-browser.stop'); break; } catch { /* Continue until publication and delivery drain. */ }
    await page.waitForTimeout(500);
    if (Date.now() - lastProgress >= 1000) {
      await writeFile('/case/load-browser-progress.tmp', JSON.stringify({
        rendered_events: new Set(Object.keys(renders).map(key => key.slice(0, key.lastIndexOf(':')))).size,
        errors: [...errors],
      }));
      await rename('/case/load-browser-progress.tmp', '/case/load-browser-progress.json');
      lastProgress = Date.now();
    }
    if (accessToken !== previousToken) {
      await page.evaluate(token => window.loadSockets.forEach(socket => socket.loadSend({ authenticate: { accessToken: token } })), accessToken);
      previousToken = accessToken;
    }
    if (Date.now() - lastPing >= 30000) {
      await page.evaluate(() => window.loadSockets.forEach(socket => socket.loadSend({ ping: {} })));
      lastPing = Date.now();
    }
  }
  const sockets = await page.evaluate(() => {
    window.loadStopping = true;
    const result = { clients: window.loadSockets.length + 1, realtime: window.loadRealtime, errors: window.loadSocketErrors };
    window.loadSockets.forEach(socket => socket.close());
    return result;
  });
  await writeFile('/case/load-browser-result.json', JSON.stringify({ renders, errors: [...errors, ...sockets.errors],
    clients: sockets.clients, realtime: sockets.realtime, incident_deliveries: incidentDeliveries, authenticated: !!accessToken }));
} finally {
  await browser.close();
}
