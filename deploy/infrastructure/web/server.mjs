import { createServer } from 'node:https';
import { createReadStream } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import { extname, resolve, sep } from 'node:path';
import { pipeline } from 'node:stream/promises';
import { createGzip } from 'node:zlib';

const root = resolve('/srv/public');
const issuer = new URL(process.env.PSIM_ISSUER);
if (issuer.protocol !== 'https:' || issuer.username || issuer.password || issuer.search || issuer.hash) {
  throw new Error('invalid identity issuer');
}
const config = Buffer.from(`window.psimConfig=${JSON.stringify({ issuer: issuer.href })};\n`);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.woff2': 'font/woff2' };
const server = createServer({
  cert: await readFile(process.env.PSIM_CERTIFICATE_FILE),
  key: await readFile(process.env.PSIM_PRIVATE_KEY_FILE),
  ca: await readFile(process.env.PSIM_CA_FILE),
  requestCert: true,
  rejectUnauthorized: true,
  minVersion: 'TLSv1.2',
}, async (request, response) => {
  response.setHeader('X-Content-Type-Options', 'nosniff');
  response.setHeader('X-Frame-Options', 'DENY');
  response.setHeader('Referrer-Policy', 'same-origin');
  response.setHeader('Content-Security-Policy', `default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self' wss://${issuer.host}; object-src 'none'; frame-ancestors 'none'; base-uri 'self'`);
  if (!['GET', 'HEAD'].includes(request.method)) {
    response.writeHead(405, { Allow: 'GET, HEAD' }); response.end(); return;
  }
  try {
    const pathname = decodeURIComponent(new URL(request.url, 'https://localhost').pathname);
    if (pathname.includes('\0') || pathname.includes('\\')) throw new Error('invalid path');
    const filename = resolve(root, '.' + pathname);
    if (filename !== root && !filename.startsWith(root + sep)) {
      response.writeHead(403); response.end(); return;
    }
    if (pathname === '/health/ready') {
      response.writeHead(200, { 'Cache-Control': 'no-store' }); response.end(request.method === 'HEAD' ? undefined : 'ready'); return;
    }
    if (pathname === '/config.js') {
      response.writeHead(200, { 'Content-Type': 'text/javascript', 'Cache-Control': 'no-store', 'Content-Length': config.length });
      response.end(request.method === 'HEAD' ? undefined : config); return;
    }
    let target = filename;
    try {
      if (!(await stat(target)).isFile()) throw new Error('not a file');
    } catch {
      if (extname(pathname) || pathname.startsWith('/assets/')) {
        response.writeHead(404); response.end(); return;
      }
      target = resolve(root, 'index.html');
    }
    response.setHeader('Content-Type', types[extname(target)] ?? 'application/octet-stream');
    response.setHeader('Cache-Control', pathname.startsWith('/assets/') ? 'public, max-age=31536000, immutable' : 'no-store');
    response.setHeader('Vary', 'Accept-Encoding');
    if (request.method === 'HEAD') { response.writeHead(200); response.end(); return; }
    const gzip = (request.headers['accept-encoding'] ?? '').split(',').some(part => {
      const [encoding, ...parameters] = part.trim().split(';');
      return encoding.toLowerCase() === 'gzip' && !parameters.some(value => /^\s*q\s*=\s*0(?:\.0*)?\s*$/i.test(value));
    });
    if (gzip) {
      response.setHeader('Content-Encoding', 'gzip');
      await pipeline(createReadStream(target), createGzip(), response);
    } else {
      await pipeline(createReadStream(target), response);
    }
  } catch {
    if (!response.headersSent) response.writeHead(400);
    response.end();
  }
});
server.requestTimeout = 15000;
server.headersTimeout = 10000;
server.keepAliveTimeout = 5000;
server.listen(Number(process.env.PSIM_PORT), process.env.PSIM_LISTEN);
for (const signal of ['SIGTERM', 'SIGINT']) {
  process.on(signal, () => {
    server.close();
    setTimeout(() => server.closeAllConnections(), 5000).unref();
  });
}
