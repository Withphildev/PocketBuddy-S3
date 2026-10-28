function integer(value, fallback) {
  const parsed = Number.parseInt(value ?? '', 10);
  return Number.isFinite(parsed) ? parsed : fallback;
}

export function loadConfig(env = process.env) {
  return {
    host: env.HOST || '0.0.0.0',
    port: integer(env.PORT, 5810),
    token: env.BUDDY_TOKEN || '',
    ollamaUrl: (env.OLLAMA_URL || 'http://192.168.0.172:11434').replace(/\/$/, ''),
    ollamaModel: env.OLLAMA_MODEL || 'gemma4:e4b',
    ollamaKeepAlive: env.OLLAMA_KEEP_ALIVE || '30m',
    buddyName: env.BUDDY_NAME || 'Ember',
    buddyOwner: env.BUDDY_OWNER || 'Phil',
    sttUrl: (env.STT_URL || '').replace(/\/$/, ''),
    sttApiKey: env.STT_API_KEY || '',
    sttModel: env.STT_MODEL || 'whisper-1',
    ttsUrl: (env.TTS_URL || '').replace(/\/$/, ''),
    ttsApiKey: env.TTS_API_KEY || '',
    ttsModel: env.TTS_MODEL || 'tts-1',
    ttsVoice: env.TTS_VOICE || 'nova',
    maxAudioBytes: integer(env.MAX_AUDIO_BYTES, 262144),
    audioTtlMs: integer(env.AUDIO_TTL_MS, 300000)
  };
}
