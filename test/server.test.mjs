import assert from 'node:assert/strict';
import { afterEach, beforeEach, test } from 'node:test';
import { createBuddyServer } from '../server/app.mjs';
import { normalizeWavLengths, resetStateForTests } from '../server/buddy.mjs';
import { resetJobsForTests } from '../server/jobs.mjs';

let server;
let baseUrl;
let lastOllamaBody;

const config = {
  host: '127.0.0.1', port: 0, token: 'secret',
  ollamaUrl: 'http://ollama.test', ollamaModel: 'gemma4:26b',
  ollamaKeepAlive: '30m',
  buddyName: 'Ember', buddyOwner: 'Phil',
  sttUrl: '', sttApiKey: '', sttModel: 'whisper-1',
  ttsUrl: '', ttsApiKey: '', ttsModel: 'tts-1', ttsVoice: 'nova',
  maxAudioBytes: 262144, audioTtlMs: 300000
};

const fakeFetch = async (url, options = {}) => {
  if (url === 'http://ollama.test/api/tags') {
    return new Response(JSON.stringify({
      models: [{ name: 'gemma4:e4b' }, { name: 'qwen3:8b' },
        { name: 'embeddinggemma:300m' }, { name: 'gemma4:e4b' }]
    }), { status: 200, headers: { 'Content-Type': 'application/json' } });
  }
  assert.equal(url, 'http://ollama.test/api/chat');
  lastOllamaBody = JSON.parse(options.body);
  return new Response(JSON.stringify({ message: { content: 'Hello from the Dark Tower!' } }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' }
  });
};

beforeEach(async () => {
  resetStateForTests();
  resetJobsForTests();
  lastOllamaBody = null;
  server = createBuddyServer(config, fakeFetch);
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

afterEach(async () => {
  server.closeAllConnections();
  await new Promise((resolve) => server.close(resolve));
});

test('health advertises capabilities without exposing secrets', async () => {
  const response = await fetch(`${baseUrl}/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), {
    ok: true,
    service: 'darktower-sticks3-buddy',
    model: 'gemma4:26b',
    speechInput: false,
    speechOutput: false
  });
});

test('chat rejects a missing token', async () => {
  const response = await fetch(`${baseUrl}/v1/chat`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text: 'Hello' })
  });
  assert.equal(response.status, 401);
});

test('lists available Dark Tower models in OpenAI-compatible form', async () => {
  const response = await fetch(`${baseUrl}/v1/models`, {
    headers: { Authorization: 'Bearer secret' }
  });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), {
    object: 'list',
    data: [
      { id: 'gemma4:e4b', object: 'model', owned_by: 'dark-tower' },
      { id: 'qwen3:8b', object: 'model', owned_by: 'dark-tower' }
    ]
  });
});

test('chat returns the model reply and an expression', async () => {
  const response = await fetch(`${baseUrl}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer secret' },
    body: JSON.stringify({ deviceId: 'test-stick', text: 'Hello' })
  });
  assert.equal(response.status, 200);
  const body = await response.json();
  assert.equal(body.text, 'Hello from the Dark Tower!');
  assert.equal(body.emotion, 'happy');
  assert.equal(body.audioPath, null);
});

test('asynchronous chat returns immediately and can be polled', async () => {
  const submitted = await fetch(`${baseUrl}/v1/jobs/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer secret' },
    body: JSON.stringify({ deviceId: 'test-stick', text: 'Hello' })
  });
  assert.equal(submitted.status, 202);
  const accepted = await submitted.json();
  assert.match(accepted.id, /^[a-f0-9-]+$/);

  let status;
  for (let attempt = 0; attempt < 20; attempt += 1) {
    const response = await fetch(`${baseUrl}${accepted.statusPath}`, {
      headers: { Authorization: 'Bearer secret' }
    });
    status = await response.json();
    if (status.state === 'complete') break;
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.equal(status.state, 'complete');
  assert.equal(status.result.text, 'Hello from the Dark Tower!');
});

test('applies a validated device personality and model profile', async () => {
  const response = await fetch(`${baseUrl}/v1/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer secret' },
    body: JSON.stringify({
      deviceId: 'profile-stick', text: 'Hello',
      profile: {
        name: 'Nova', owner: 'Phil', model: 'gemma4:e4b', voice: 'af_sky',
        personality: 'Cheerful, practical, and fascinated by space.'
      }
    })
  });
  assert.equal(response.status, 200);
  assert.equal(lastOllamaBody.model, 'gemma4:e4b');
  assert.match(lastOllamaBody.messages[0].content, /You are Nova, Phil's tiny desktop companion/);
  assert.match(lastOllamaBody.messages[0].content, /fascinated by space/);
});

test('finalizes streaming WAV placeholder lengths for embedded playback', () => {
  const wav = Buffer.alloc(52);
  wav.write('RIFF', 0, 'ascii');
  wav.writeUInt32LE(0xffffffff, 4);
  wav.write('WAVEfmt ', 8, 'ascii');
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(24000, 24);
  wav.writeUInt32LE(48000, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write('data', 36, 'ascii');
  wav.writeUInt32LE(0xffffffff, 40);

  normalizeWavLengths(wav);
  assert.equal(wav.readUInt32LE(4), 44);
  assert.equal(wav.readUInt32LE(40), 8);
});
