// SPDX-License-Identifier: GPL-3.0-only
// MEL in an AudioWorklet: mel.wasm is the whole firmware against a fake FM-1 (web/emu/fm1_web.c). Each render
// quantum asks it for 128 frames, which runs the device's clock forward; between quanta the page's input goes in
// and, ~30 times a second, the screen, the LEDs and changed settings go out. (After X0X's / Felucca's worklet.)

const HOLD_MS = 20;               // each key / button change stays this long (device time): the app reads them once a ms

class Mel extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.queue = [];
    this.nextOk = 0;
    this.sentDraws = -1;
    this.sentWrites = -1;
    this.sentLeds = "";
    this.lastFrame = 0;
    this.busy = 0;
    this.frames = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const ex = instance.exports;
        if (m.settings && m.settings.length) {
          new Uint8Array(ex.memory.buffer, ex.web_settings(), m.settings.length).set(m.settings);
          ex.web_settings_set_len(m.settings.length);
        }
        ex.web_master(m.master ?? 800);
        ex.web_boot();
        this.ex = ex;
        this.sentWrites = ex.web_settings_writes();
        this.port.postMessage({ type: "ready" });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
      return;
    }
    const ex = this.ex;
    if (!ex) return;
    if (m.type === "keys" || m.type === "button") this.queue.push(m);
    else if (m.type === "enc") ex.web_enc(m.id, m.n | 0);
    else if (m.type === "master") ex.web_master(m.value | 0);
    else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
  }

  input() {
    const ex = this.ex, now = ex.web_now_ms();
    while (this.queue.length && now - this.nextOk >= 0) {
      const m = this.queue.shift();
      if (m.type === "keys") ex.web_keys(m.mask >>> 0);
      else ex.web_button(m.id, m.down ? 1 : 0);
      this.nextOk = now + HOLD_MS;
    }
  }

  publish() {
    const ex = this.ex, mem = ex.memory.buffer, msg = { type: "frame" }, transfer = [];
    const leds = ex.web_leds(0) + "," + ex.web_leds(1);
    if (leds !== this.sentLeds) { this.sentLeds = leds; msg.leds = [ex.web_leds(0) >>> 0, ex.web_leds(1) >>> 0]; }
    const draws = ex.web_screen_draws();
    if (draws !== this.sentDraws) {
      this.sentDraws = draws;
      msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    const writes = ex.web_settings_writes();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      msg.settings = Array.from(new Uint8Array(mem, ex.web_settings(), Math.max(0, ex.web_settings_len())));
    }
    if (this.frames >= sampleRate) {
      msg.load = this.busy / (this.frames / sampleRate * 1000);
      this.busy = this.frames = 0;
    }
    if (msg.leds || msg.fb || msg.settings || msg.load !== undefined) this.port.postMessage(msg, transfer);
  }

  process(inputs, outputs) {
    const ex = this.ex;
    if (!ex) return true;
    if (this.queue.length) this.input();
    const out = outputs[0], n = out[0].length, t0 = Date.now();
    ex.web_render(n);
    this.busy += Date.now() - t0;
    this.frames += n;
    out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
    if (currentTime - this.lastFrame >= 1 / 30) {
      this.lastFrame = currentTime;
      this.publish();
    }
    return true;
  }
}
registerProcessor("mel", Mel);
