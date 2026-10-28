import { createBuddyServer } from './app.mjs';
import { loadConfig } from './config.mjs';

const config = loadConfig();
const server = createBuddyServer(config);
server.listen(config.port, config.host, () => {
  console.log(`StickS3 buddy bridge listening on http://${config.host}:${config.port}`);
  console.log(`Ollama: ${config.ollamaUrl} (${config.ollamaModel})`);
  console.log(`Speech input: ${config.sttUrl ? 'configured' : 'disabled'}`);
  console.log(`Speech output: ${config.ttsUrl ? 'configured' : 'disabled'}`);
});
