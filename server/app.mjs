import { createServer } from 'node:http';
import { converse, takeAudio, transcribe } from './buddy.mjs';
import { jobStatus, startJob } from './jobs.mjs';

function sendJson(res, status, value) {
  const body = Buffer.from(JSON.stringify(value));
  res.writeHead(status, {
    'Content-Type': 'application/json',
    'Content-Length': body.length,
    'Cache-Control': 'no-store'
  });
  res.end(body);
}

async function readBody(req, limit) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > limit) {
      const error = new Error('Request body is too large');
      error.statusCode = 413;
      throw error;
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function isAuthorized(req, config) {
  if (!config.token) return true;
  return req.headers.authorization === `Bearer ${config.token}`;
}

async function discoverModels(config, fetchImpl) {
  const response = await fetchImpl(`${config.ollamaUrl}/api/tags`);
  if (!response.ok) throw new Error(`Ollama model discovery returned HTTP ${response.status}`);
  const body = await response.json();
  const ids = [...new Set((body.models || [])
    .map((item) => String(item.name || item.model || '').trim())
    .filter((id) => /^[A-Za-z0-9_.:\/-]{1,80}$/.test(id))
    .filter((id) => !/(^|[-_:])(embed|embedding)/i.test(id)))].sort();
  return {
    object: 'list',
    data: ids.map((id) => ({ id, object: 'model', owned_by: 'dark-tower' }))
  };
}

function safeProfile(input, req, config) {
  const body = input?.profile || {};
  const clean = (value, fallback, max) => {
    const text = String(value ?? '').replace(/[\r\n\0]/g, ' ').trim();
    return text ? text.slice(0, max) : fallback;
  };
  const model = clean(body.model ?? req.headers['x-buddy-model'], config.ollamaModel, 80);
  return {
    name: clean(body.name ?? req.headers['x-buddy-name'], config.buddyName, 32),
    owner: clean(body.owner ?? req.headers['x-buddy-owner'], config.buddyOwner, 32),
    personality: clean(body.personality ?? req.headers['x-buddy-personality'], '', 600),
    model: /^[A-Za-z0-9_.:\/-]{1,80}$/.test(model) ? model : config.ollamaModel,
    voice: clean(body.voice ?? req.headers['x-buddy-voice'], config.ttsVoice, 64)
  };
}

export function createBuddyServer(config, fetchImpl = fetch) {
  return createServer(async (req, res) => {
    const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    try {
      if (req.method === 'GET' && url.pathname === '/health') {
        return sendJson(res, 200, {
          ok: true,
          service: 'darktower-sticks3-buddy',
          model: config.ollamaModel,
          speechInput: Boolean(config.sttUrl),
          speechOutput: Boolean(config.ttsUrl)
        });
      }

      if (!isAuthorized(req, config)) return sendJson(res, 401, { error: 'Unauthorized' });

      if (req.method === 'GET' && url.pathname === '/v1/models') {
        return sendJson(res, 200, await discoverModels(config, fetchImpl));
      }

      if (req.method === 'POST' && url.pathname === '/v1/chat') {
        const raw = await readBody(req, 8192);
        let input;
        try { input = JSON.parse(raw.toString('utf8')); }
        catch { return sendJson(res, 400, { error: 'Body must be valid JSON' }); }
        const text = input.text?.trim();
        const deviceId = input.deviceId?.trim() || 'sticks3';
        if (!text || text.length > 1000) return sendJson(res, 400, { error: 'text is required and must be at most 1000 characters' });
        return sendJson(res, 200, await converse(config, fetchImpl, deviceId, text, safeProfile(input, req, config)));
      }

      if (req.method === 'POST' && url.pathname === '/v1/jobs/chat') {
        const raw = await readBody(req, 8192);
        let input;
        try { input = JSON.parse(raw.toString('utf8')); }
        catch { return sendJson(res, 400, { error: 'Body must be valid JSON' }); }
        const text = input.text?.trim();
        const deviceId = input.deviceId?.trim() || 'sticks3';
        if (!text || text.length > 1000) return sendJson(res, 400, { error: 'text is required and must be at most 1000 characters' });
        const id = startJob(() => converse(config, fetchImpl, deviceId, text, safeProfile(input, req, config)));
        return sendJson(res, 202, { id, state: 'queued', statusPath: `/v1/jobs/${id}` });
      }

      if (req.method === 'POST' && url.pathname === '/v1/talk') {
        const bytes = await readBody(req, config.maxAudioBytes);
        if (!bytes.length) return sendJson(res, 400, { error: 'WAV body is required' });
        const deviceId = String(req.headers['x-device-id'] || 'sticks3').slice(0, 80);
        const transcript = await transcribe(config, fetchImpl, bytes);
        const result = await converse(config, fetchImpl, deviceId, transcript, safeProfile(null, req, config));
        return sendJson(res, 200, { transcript, ...result });
      }

      if (req.method === 'POST' && url.pathname === '/v1/jobs/talk') {
        const bytes = await readBody(req, config.maxAudioBytes);
        if (!bytes.length) return sendJson(res, 400, { error: 'WAV body is required' });
        const deviceId = String(req.headers['x-device-id'] || 'sticks3').slice(0, 80);
        const profile = safeProfile(null, req, config);
        const id = startJob(async () => {
          const transcript = await transcribe(config, fetchImpl, bytes);
          const result = await converse(config, fetchImpl, deviceId, transcript, profile);
          return { transcript, ...result };
        });
        return sendJson(res, 202, { id, state: 'queued', statusPath: `/v1/jobs/${id}` });
      }

      const jobMatch = url.pathname.match(/^\/v1\/jobs\/([a-f0-9-]+)$/i);
      if (req.method === 'GET' && jobMatch) {
        const status = jobStatus(jobMatch[1]);
        if (!status) return sendJson(res, 404, { error: 'Job expired or was not found' });
        return sendJson(res, 200, status);
      }

      const audioMatch = url.pathname.match(/^\/v1\/audio\/([a-f0-9-]+)$/i);
      if (req.method === 'GET' && audioMatch) {
        const bytes = takeAudio(audioMatch[1]);
        if (!bytes) return sendJson(res, 404, { error: 'Audio expired or was not found' });
        res.writeHead(200, {
          'Content-Type': 'audio/wav',
          'Content-Length': bytes.length,
          'Cache-Control': 'no-store'
        });
        return res.end(bytes);
      }

      return sendJson(res, 404, { error: 'Not found' });
    } catch (error) {
      const status = error.statusCode || 502;
      console.error(error);
      return sendJson(res, status, { error: error.message || 'Bridge failure' });
    }
  });
}
