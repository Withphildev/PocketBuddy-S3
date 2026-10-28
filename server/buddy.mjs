import { randomUUID } from 'node:crypto';

const sessions = new Map();
const audioCache = new Map();

export function normalizeWavLengths(bytes) {
  if (bytes.length < 44 || bytes.toString('ascii', 0, 4) !== 'RIFF' ||
      bytes.toString('ascii', 8, 12) !== 'WAVE') return bytes;

  bytes.writeUInt32LE(bytes.length - 8, 4);
  let offset = 12;
  while (offset + 8 <= bytes.length) {
    const id = bytes.toString('ascii', offset, offset + 4);
    const size = bytes.readUInt32LE(offset + 4);
    if (id === 'data') {
      bytes.writeUInt32LE(bytes.length - offset - 8, offset + 4);
      break;
    }
    if (size === 0xffffffff || offset + 8 + size > bytes.length) break;
    offset += 8 + size + (size & 1);
  }
  return bytes;
}

function authHeaders(apiKey, extra = {}) {
  return apiKey
    ? { ...extra, Authorization: `Bearer ${apiKey}` }
    : extra;
}

function emotionFor(text) {
  const value = text.toLowerCase();
  if (/sorry|sad|unfortunately|can't|cannot/.test(value)) return 'concerned';
  if (/great|wonderful|glad|happy|yes|absolutely|!/.test(value)) return 'happy';
  if (/think|perhaps|maybe|consider/.test(value)) return 'thinking';
  return 'calm';
}

function remember(deviceId, userText, assistantText) {
  const history = sessions.get(deviceId) || [];
  history.push({ role: 'user', content: userText });
  history.push({ role: 'assistant', content: assistantText });
  sessions.set(deviceId, history.slice(-8));
}

export async function transcribe(config, fetchImpl, wavBytes) {
  if (!config.sttUrl) {
    throw new Error('Speech recognition is not configured on the bridge');
  }
  const form = new FormData();
  form.append('model', config.sttModel);
  form.append('file', new Blob([wavBytes], { type: 'audio/wav' }), 'buddy.wav');
  const response = await fetchImpl(`${config.sttUrl}/v1/audio/transcriptions`, {
    method: 'POST',
    headers: authHeaders(config.sttApiKey),
    body: form
  });
  if (!response.ok) throw new Error(`STT returned HTTP ${response.status}`);
  const body = await response.json();
  if (!body.text?.trim()) throw new Error('STT returned no transcript');
  return body.text.trim();
}

export async function askOllama(config, fetchImpl, deviceId, text, profile = {}) {
  const history = sessions.get(deviceId) || [];
  const buddyName = profile.name || config.buddyName;
  const ownerName = profile.owner || config.buddyOwner;
  const system = [
    `You are ${buddyName}, ${ownerName}'s tiny desktop companion.`,
    profile.personality ? `Your personality: ${profile.personality}` : '',
    'Reply warmly and naturally in no more than two short sentences.',
    'You are speaking through a very small device, so do not use markdown or lists.'
  ].filter(Boolean).join(' ');
  const response = await fetchImpl(`${config.ollamaUrl}/api/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      model: profile.model || config.ollamaModel,
      stream: true,
      think: false,
      keep_alive: config.ollamaKeepAlive,
      messages: [{ role: 'system', content: system }, ...history, { role: 'user', content: text }],
      options: { temperature: 0.7, num_predict: 90 }
    })
  });
  if (!response.ok) throw new Error(`Ollama returned HTTP ${response.status}`);
  const decoder = new TextDecoder();
  let pending = '';
  let collected = '';

  function consume(line) {
    if (!line.trim()) return;
    const event = JSON.parse(line);
    if (event.error) throw new Error(`Ollama error: ${event.error}`);
    if (event.message?.content) collected += event.message.content;
  }

  for await (const chunk of response.body) {
    pending += decoder.decode(chunk, { stream: true });
    const lines = pending.split('\n');
    pending = lines.pop() || '';
    for (const line of lines) consume(line);
  }
  pending += decoder.decode();
  if (pending.trim()) consume(pending);

  const reply = collected.trim();
  if (!reply) throw new Error('Ollama returned no reply');
  remember(deviceId, text, reply);
  return reply;
}

export async function synthesize(config, fetchImpl, text, voice = config.ttsVoice) {
  if (!config.ttsUrl) return null;
  const response = await fetchImpl(`${config.ttsUrl}/v1/audio/speech`, {
    method: 'POST',
    headers: authHeaders(config.ttsApiKey, { 'Content-Type': 'application/json' }),
    body: JSON.stringify({
      model: config.ttsModel,
      voice,
      input: text,
      response_format: 'wav'
    })
  });
  if (!response.ok) throw new Error(`TTS returned HTTP ${response.status}`);
  const bytes = normalizeWavLengths(Buffer.from(await response.arrayBuffer()));
  const id = randomUUID();
  audioCache.set(id, { bytes, expiresAt: Date.now() + config.audioTtlMs });
  return id;
}

export async function converse(config, fetchImpl, deviceId, text, profile = {}) {
  const reply = await askOllama(config, fetchImpl, deviceId, text, profile);
  const audioId = await synthesize(config, fetchImpl, reply, profile.voice || config.ttsVoice);
  return {
    id: randomUUID(),
    text: reply,
    emotion: emotionFor(reply),
    audioPath: audioId ? `/v1/audio/${audioId}` : null
  };
}

export function takeAudio(id) {
  const item = audioCache.get(id);
  if (!item || item.expiresAt < Date.now()) {
    audioCache.delete(id);
    return null;
  }
  return item.bytes;
}

export function resetStateForTests() {
  sessions.clear();
  audioCache.clear();
}
