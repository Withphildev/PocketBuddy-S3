# PocketBuddy milestone status

Date: 2026-10-27

## Completed

- Dark Tower bridge with token authentication and short per-device memory
- Direct Ollama chat integration using dedicated buddy model `gemma4:e4b`
- Asynchronous submit-and-poll jobs for slow Dark Tower inference
- Optional OpenAI-compatible STT and WAV TTS adapters
- PocketSDK-based StickS3 firmware with expressions, buttons, mic, and speaker
- Secret-safe local firmware configuration
- Docker Compose deployment definition
- Seven passing bridge tests
- Clean PlatformIO firmware compile
- Verified combined 8 MB ESP32-S3 factory image for flashing at `0x0000`
- First-boot USB serial provisioning stores Wi-Fi and bridge settings in NVS,
  keeping credentials out of the distributable binary
- Factory image flashed by the user; USB provisioning accepted all required
  settings, saved them to NVS, and completed a clean software restart
- Physical conversation testing proved Dark Tower STT and TTS generation, but
  exposed single-line display truncation and a device-side WAV playback failure
- Firmware v0.2 wraps replies across three lines and delegates WAV parsing and
  playback to M5Unified with automatic speaker-channel selection
- Physical USB diagnostics isolated playback rejection to Kokoro's streaming
  WAV placeholders (`0xFFFFFFFF` RIFF/data lengths); the bridge now finalizes
  both lengths before caching audio, and all five bridge tests pass
- Physical playback succeeded after WAV finalization; firmware speaker volume
  raised from 200 to the maximum software level of 255 for the small enclosure
- Physical listening confirmed clear, acceptable voice output at 255 on the
  StickS3's 1 W speaker
- Read-only Dark Tower probe confirming Ollama and `gemma4:26b` are available at
  `192.168.0.172:11434`
- Live bridge smoke test showed `gemma4:26b` exceeded 180 seconds; the protocol
  was upgraded to asynchronous jobs and revalidated afterward
- A deployed five-minute test exposed Node's response-header timeout when
  Ollama used `stream:false`; the bridge now consumes Ollama's token stream
  internally while exposing only polled job states to the StickS3
- `gemma4:e4b` measured about 265 seconds cold (264.6 seconds loading) and 0.17
  seconds warm; PocketBuddy now uses it with a 30-minute keep-alive without
  changing Nova/OpenClaw's model route
- The model initially spent its short reply budget entirely on hidden reasoning;
  PocketBuddy now requests `think:false`, producing a user-facing warm response
  in about 0.15 seconds without changing Nova's thinking configuration
- PocketSDK README, changelog, and PocketBuddy integration documentation updated
- Deployed on Dark Tower as `sticks3-buddy` at `192.168.0.172:5810` with
  `restart: unless-stopped`
- Final authenticated submit/poll conversation completed in 446 ms and returned:
  “Beep boop PocketBuddy is all connected and ready for fun!”
- Private speech services deployed on the buddy Compose network: GPU-accelerated
  Faster Whisper for English transcription and CPU Kokoro for 24 kHz mono WAV
  synthesis; neither service publishes a LAN port
- A container health warm-up loads the speech model after service startup and
  keeps it resident, using about 512 MiB of GPU memory, so normal conversation
  does not pay its roughly 30-second load time
- End-to-end audio upload, transcription, Ollama reply, speech synthesis, and
  authenticated audio retrieval passed in 1.891 seconds with warm services
- Authenticated responsive WebUI embedded in the StickS3 firmware with fields
  for Wi-Fi, OpenAI-compatible bridge connection, model, voice, volume, buddy
  identity, owner, device ID, and personality bio
- Setup hotspot fallback with captive DNS; its SSID, password, and setup address
  are shown on the StickS3 for first-time configuration
- WebUI API never returns Wi-Fi or API-key secrets; blank secret fields retain
  existing values
- Live WebUI GET and Save & Restart testing passed on the physical StickS3 at
  both its numeric LAN address and `pocketbuddy.local`; saved credentials
  survived the restart
- Per-device WebUI personality, model, and voice settings now accompany bridge
  chat and speech requests without changing global Dark Tower services
- Firmware v0.4 compiled at 16.8% RAM and 43.6% application-flash usage, flashed
  successfully, and reconnected with its existing NVS settings intact
- Firmware v0.5 adds an on-screen Wi-Fi QR code and device-specific portal login
  for clean first-boot setup
- Captive DNS makes `http://pocketbuddy` resolve while connected directly to
  the StickS3 hotspot; `192.168.4.1` remains available as a fallback
- The same authenticated WebUI is served in setup-hotspot and normal LAN modes
- Dark Tower now exposes an authenticated OpenAI-compatible `/v1/models` list;
  the WebUI discovers chat-capable models and saves the selection as the device
  default while filtering embedding-only models
- Firmware v0.5 compiled successfully at 16.8% RAM and 43.8% application-flash
  usage; it was intentionally not flashed because the user will install the
  clean `0x0000` factory binary manually
- Physical v0.5 setup proved the Wi-Fi QR connection and captive WebUI, then
  exposed an overly strict save gate when the API key was still blank
- Firmware v0.6 enlarges the QR modules, allows Wi-Fi-only staged setup, joins
  the LAN before AI credentials are complete, and replaces the generic save
  failure with specific Wi-Fi or device-storage errors
- Firmware v0.6 compiled successfully at 16.8% RAM and 43.8% application-flash
  usage and remains unflashed for manual installation at `0x0000`
- Firmware v0.7 replaces the primitive eye blocks with nine transparent,
  reference-matched illustrated states: calm, listening, thinking left/right,
  happy open/closed, concerned, sleepy/offline, and blink
- Calm and happy blink on a randomized interval. Thinking now randomly selects
  the left or right gaze when the state begins and holds that single image,
  reducing display redraws and battery use while keeping both expressions
- The generated source art was normalized into 240×80 alpha PNG layers and
  embedded in flash; captions retain the lower 55 pixels of the display
- Firmware v0.7 compiled successfully at 16.8% RAM and 48.2% application-flash
  usage and was intentionally left unflashed for manual installation
- Firmware v0.7.1 replaces the continuous 650 ms thinking animation with one
  randomly selected gaze per thinking state and remains intentionally unflashed

## Physical validation still required

- Continue tuning microphone gain and personality behavior during normal use

No existing Nova/OpenClaw, AIRI, Ollama, or unrelated Dark Tower service was
changed by this milestone.

Dark Tower SSH now uses a dedicated `pocketbuddy-darktower` ED25519 key. The
bridge token exists only in Dark Tower's protected `.env`; it was not printed or
stored in the project source.
