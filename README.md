# Dark Tower StickS3 Buddy

This prototype turns an M5Stack StickS3 into a small face, microphone, speaker,
and button client for the Dark Tower. The bridge keeps model credentials and
conversation state off the device and uses Dark Tower's existing Ollama service.

## What works in this milestone

- PocketSDK initialization and logical Face/M5 button handling
- Animated listening, thinking, reply, error, and offline expressions
- Authenticated LAN bridge
- Submit-and-poll jobs so slow local-model inference never holds one ESP32
  request open
- M5 button text-path test against Dark Tower's dedicated `gemma4:e4b` buddy
  model
- Face button four-second 16 kHz mono WAV capture
- Private Faster Whisper speech-to-text and Kokoro WAV text-to-speech services
- Short per-device conversation history
- Authenticated local WebUI for Wi-Fi, service URL, model, voice, volume, device
  identity, and personality settings
- Automatic setup hotspot when saved Wi-Fi is missing or unavailable
- On-screen Wi-Fi QR code for joining the setup hotspot
- Live Dark Tower chat-model discovery and default-model selection
- Nine-state illustrated eye pack with calm blinking, listening accents, a
  randomly selected static thinking gaze, happy animation, concern, and sleepy
  offline state
- Docker deployment and bridge tests

The M5 button path works independently of speech services. The Face button uses
the private `buddy-stt` and `buddy-tts` containers on the Compose network; only
the authenticated bridge is published to the LAN.

Faster Whisper runs on the RTX A2000 with the small distilled English model. A
container health warm-up loads the model after service startup and it remains
resident because its measured GPU footprint is about 512 MiB, avoiding a
roughly 30-second reload after idle periods. Kokoro runs on CPU and returns 24
kHz, mono, 16-bit PCM WAV that the StickS3 firmware can play directly. A warm
end-to-end voice loop completed in 1.891 seconds.

## 1. Configure and run the bridge

```sh
cp .env.example .env
```

Edit `.env`. Generate one long random value for `BUDDY_TOKEN` and use the same
value in the firmware. On Dark Tower:

```sh
docker compose up -d --build
curl http://127.0.0.1:5810/health
```

The bridge defaults to Dark Tower's Ollama at `192.168.0.172:11434`. If the
bridge container cannot reach that address, set `OLLAMA_URL` to the appropriate
Docker-accessible host address.

## 2. Flash and configure the StickS3

Flash `PocketBuddy-StickS3-v0.7.1-eyes-factory-0x0000.bin` at `0x0000`. This merged
factory image covers the settings partition with erased bytes, providing the
requested clean start. On first boot, PocketBuddy creates a
`PocketBuddy-XXXX` hotspot and displays a standard Wi-Fi QR code. Scan it to
join, then open `http://pocketbuddy`. `http://192.168.4.1` remains the fallback.
The screen also shows the Basic Auth username and device-specific password.

After PocketBuddy joins Wi-Fi, open `http://pocketbuddy.local`. Its numeric LAN
address is also shown on the StickS3 screen and in the page's Online indicator.
The WebUI configures:

- Wi-Fi SSID and password
- OpenAI-compatible PocketBuddy bridge URL and API key
- default chat model, discovered live from Dark Tower, and TTS voice
- buddy name, owner name, device ID, and personality bio
- speaker volume

Saving restarts the StickS3. Existing Wi-Fi and API secrets are preserved when
their fields are left blank, and neither secret is returned to the browser.
On a completely clean device, Wi-Fi can be saved by itself. PocketBuddy then
joins the LAN and shows its WebUI address with a reminder that AI setup is still
incomplete. Reopen the WebUI, add the API key, and use **Refresh** beside the
model picker to select any available Dark Tower chat model as the default.
The USB serial console remains available at 115200 baud; enter `STATUS` to see
the LAN URL and WebUI login, `HELP` for commands, or `CLEAR` to erase setup.
Credentials are saved in NVS and are not embedded in the public binary.

For a source build, copy `firmware/include/config.local.h.example` to
`firmware/include/config.local.h`, then edit the local file with the Wi-Fi name,
password, bridge token, and bridge URL. The local file is ignored by Git. Then:

```sh
pio run -d firmware -t upload
pio device monitor -d firmware
```

- Press **M5** to test the bridge and model without recording audio.
- Press **Face** to record four seconds and use the speech path.

The illustrated eyes occupy a transparent 240×80 face layer above the caption.
Calm and happy states blink automatically. Each time PocketBuddy enters its
thinking state it randomly chooses the left or right gaze and holds that image,
avoiding continuous redraws. Listening, concerned, and offline states use
dedicated artwork. Source PNGs and normalized firmware sprites live under
`firmware/assets/eyes`.

Both controls submit a job and poll its status. This is intentional because a
cold local model can take several minutes to load, longer than the StickS3 HTTP
client's safe per-request timeout. Dark Tower testing measured `gemma4:e4b` at
about 265 seconds cold but 0.17 seconds warm, so the bridge keeps it resident for
30 minutes after use. PocketBuddy disables the model's internal thinking mode so
its short token budget is reserved for the spoken answer. This does not change
Nova/OpenClaw's model route or thinking behavior.

## 3. Verify from another LAN machine

```sh
curl http://192.168.0.172:5810/health
curl -H 'Authorization: Bearer YOUR_TOKEN' \
  -H 'Content-Type: application/json' \
  -d '{"deviceId":"manual-test","text":"Say hello in one sentence."}' \
  http://192.168.0.172:5810/v1/chat
```

## Security boundary

Port 5810 is intended for the trusted LAN only. The token prevents casual LAN
use but is not a substitute for TLS on an untrusted network. Do not expose this
service directly to the internet. Real Wi-Fi and service credentials belong in
the device's NVS and Dark Tower's untracked `.env`; they must not be committed
to source.

## PocketSDK relationship

PocketBuddy deliberately uses PocketSDK for device startup, display access,
battery sampling, and logical button events. Wi-Fi, HTTP, the buddy protocol,
audio policy, and UI remain application-owned, matching PocketSDK's current
architecture rules. PocketSDK's README, changelog, and integration notes were
updated alongside this deliverable.

The firmware currently pins PocketSDK commit
`a9dc0dcfa556ed965c98bd11daa22c3b094fd452`. The repository documents a future
`v0.1.0` pin, but that Git tag was not present when this prototype was built.
