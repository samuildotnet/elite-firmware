# Web Flasher

Static page that lets a non-technical user flash the firmware **from
the browser** via the [Web Serial API][1]. No installs, no drivers
beyond what Windows/macOS/Linux ship out of the box for ESP32-C3.

## How it works

1. Page is hosted at https://flash.elite.prygoda.xyz (HTTPS required —
   Web Serial refuses to run over plain HTTP).
2. Browser fetches `manifests/<device>.json`.
3. The manifest points at a pre-compiled `*.factory.bin` artifact.
4. [ESP Web Tools][2] (loaded from CDN) talks to the chip directly
   over USB and writes the binary to flash at offset `0x0`.

## Building

The `bin/` directory is populated by the GitHub Actions workflow
`.github/workflows/build.yml` — every push to `main` produces a fresh
`elite-deye-12k-01.factory.bin` artifact, which the deploy step copies
into `web-flasher/bin/` before publishing.

For local dev:

```bash
esphome compile firmware/devices/elite-deye-12k-01.yaml
mkdir -p web-flasher/bin
cp .esphome/build/elite-deye-12k-01/.pioenvs/*/firmware-factory.bin \
   web-flasher/bin/elite-deye-12k-01.factory.bin
python -m http.server -d web-flasher 8000
# open http://localhost:8000 in Chrome
```

> Note: Web Serial **requires HTTPS** in production. `localhost` is
> exempt for dev.

## Deployment

Either:

- **GitHub Pages** — enable Pages on this repo, source = `web-flasher/`
  on `main`. Free, but exposes pre-compiled binary.
- **Caddy on our VPS** — serves at `flash.elite.prygoda.xyz` with
  Let's Encrypt cert. Caddy config snippet:

  ```caddyfile
  flash.elite.prygoda.xyz {
    encode gzip zstd
    root * /srv/elite/flash
    file_server
  }
  ```

  See `samuildotnet/elite-energy` repo (PR landing alongside the EMQX
  hardening pass) for the full Caddyfile + DNS instructions.

[1]: https://wicg.github.io/serial/
[2]: https://esphome.github.io/esp-web-tools/
