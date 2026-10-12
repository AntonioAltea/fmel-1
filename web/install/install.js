// SPDX-License-Identifier: GPL-3.0-only
// MEL's web installer: Felucca's update protocol (fm1ota.js, fm1pkg.js, unchanged) with MEL's checks on top,
// the same as tools/fm1_install.py: the package is whole and is MEL; the FM-1 runs M-VAVE's own firmware, the USB
// rescue or MEL (never straight over another custom firmware); an install cut off is finished, never started
// over; the official V15 can always go back on.
import { Updater } from "./fm1ota.js";
import { Package, logicalImage, sha256hex, validateStockPackage } from "./fm1pkg.js";

const $ = (id) => document.getElementById(id);
const say = (el, text, cls = "") => { $(el).textContent = text; $(el).className = cls; };
const log = (t) => { $("log").textContent += t + "\n"; $("log").scrollTop = 1e9; };

let mel = null;            // {product, image, version}
let access = null;
let busy = false;
let device = null;         // the identity found: {text, model, version}

// MEL is FM-1_95N; M-VAVE's own firmware is FM-1_0NN (FM-1_000: the USB rescue of a Felucca-family firmware)
const isMel = (id) => /^FM-1_95\d$/.test(id.text);
const isOfficialOrRescue = (id) => /^FM-1_0\d\d$/.test(id.text);
const isLoader = (id) => /^ota-/i.test(id.model);
const isOurLoader = (id) => /^ota-FM-1_9\d\d$/i.test(id.text);   // Felucca's loader, as MEL's package carries

// ---- the package: whole (its CRCs, its SHA-256 as published), and MEL
async function loadPackage() {
  try {
    const meta = await (await fetch("mel.json", { cache: "no-store" })).json();
    const bytes = new Uint8Array(await (await fetch(meta.file, { cache: "no-store" })).arrayBuffer());
    if (await sha256hex(bytes) !== meta.sha256) throw new Error("el archivo no coincide con su huella (SHA-256): recarga la página");
    const pkg = new Package(bytes);                       // throws on any CRC mismatch
    if (!/^FM-1_95\d$/.test(pkg.product) || pkg.product !== meta.product) throw new Error(`identidad inesperada: ${pkg.product}`);
    mel = { product: pkg.product, image: logicalImage(bytes), version: meta.version };
    say("pkg", `MEL ${meta.version} (${pkg.product}), ${Math.round(bytes.length / 1024)} KB: comprobado.`, "ok");
    log(`package ${meta.file} sha256 ${meta.sha256}`);
  } catch (e) {
    mel = null;
    say("pkg", `No se puede usar el paquete: ${e.message}. No se instalará nada.`, "bad");
  }
  refresh();
}

function prechecked() { return [...document.querySelectorAll(".pre")].every((c) => c.checked); }

function refresh() {
  const ready = mel && device && !busy && prechecked();
  $("install").disabled = !(ready && (isOfficialOrRescue(device) || isMel(device)));
  $("resume").hidden = !(device && isOurLoader(device));
  $("resume").disabled = !(mel && device && !busy && prechecked());
  $("connect").disabled = busy;
  $("stock").disabled = busy || !$("stockfile").files.length || !prechecked() || !device || isLoader(device);
}

// ---- the FM-1: what it runs decides what may happen
async function connect() {
  try {
    if (!navigator.requestMIDIAccess) throw new Error("este navegador no tiene Web MIDI: usa Chrome o Edge");
    access = access || await navigator.requestMIDIAccess({ sysex: true });
    say("dev", "Buscando…", "muted");
    const dev = await new Updater(access, log).find();
    if (!dev) { device = null; say("dev", "No encuentro el FM-1: ¿cable de datos?, ¿otra app lo usa? Desconecta, vuelve a conectar y prueba otra vez.", "bad"); return refresh(); }
    dev.link.close();
    device = dev.id;
    log(`device ${device.text} on ${dev.name}`);
    if (isLoader(device) && isOurLoader(device))
      say("dev", `El FM-1 está en modo actualización (${device.text}): una instalación se cortó. Pulsa «Terminar la instalación».`, "bad");
    else if (isLoader(device))
      say("dev", `El FM-1 está en modo actualización de otro firmware (${device.text}): termina esa actualización con su propio instalador (M-UPGRADE) y vuelve.`, "bad");
    else if (isMel(device))
      say("dev", `El FM-1 lleva MEL (${device.text}): se puede actualizar.`, "ok");
    else if (device.text === "FM-1_000")
      say("dev", "El FM-1 está en el rescate USB: se puede instalar.", "ok");
    else if (isOfficialOrRescue(device))
      say("dev", `El FM-1 lleva el firmware oficial (${device.text}): se puede instalar.`, "ok");
    else
      say("dev", `El FM-1 lleva otro firmware no oficial (${device.text}). Primero vuelve al V15 oficial (apartado 4) y después instala MEL.`, "bad");
  } catch (e) {
    say("dev", `Error: ${e.message}`, "bad");
  }
  refresh();
}

function progress(total) {
  return (k, n) => {
    if (k === "start") say("status", `Empezando en ${n}…`);
    else if (k === "verify") say("status", `El FM-1 comprueba el paquete (${n})…`);
    else if (k === "loader") say("status", "Pasando a modo actualización… no lo desconectes.");
    else if (k === "write") { $("bar").hidden = false; $("bar").value = Math.min(99, Math.round(n * 512 * 100 / total)); say("status", "Escribiendo… no lo desconectes."); }
    else if (k === "reboot") say("status", "Reiniciando…");
    else if (k === "done") { $("bar").value = 100; say("status", `Hecho: el FM-1 lleva ${n}. Ya puedes desconectarlo.`, "ok"); }
    log(`${k} ${n ?? ""}`);
  };
}

async function guarded(work) {
  busy = true; refresh();
  const warn = (e) => { e.preventDefault(); e.returnValue = ""; };
  window.addEventListener("beforeunload", warn);
  try { await work(); }
  catch (e) {
    say("status", e.message, "bad");
    log(`error ${e.code || ""} ${e.message}`);
  } finally {
    window.removeEventListener("beforeunload", warn);
    busy = false; device = null; refresh();
  }
}

$("install").onclick = () => guarded(async () => {
  if (!confirm("¿Instalar MEL? No desconectes el FM-1 hasta que la página diga «Hecho».")) return;
  await connect();                                           // what it runs now, not when the page looked
  if (!device || !(isOfficialOrRescue(device) || isMel(device))) throw new Error("El FM-1 ya no está listo para instalar: búscalo otra vez.");
  await new Updater(access, log).install(mel.image, mel.product, progress(mel.image.length));
});

$("resume").onclick = () => guarded(async () => {
  const ok = await new Updater(access, log).resume(mel.image, progress(mel.image.length), mel.product);
  if (!ok) throw new Error("No he podido terminar: desconecta, vuelve a conectar y busca el FM-1 otra vez.");
});

$("stock").onclick = () => guarded(async () => {
  const bytes = new Uint8Array(await $("stockfile").files[0].arrayBuffer());
  const stock = await validateStockPackage(bytes);           // only the unmodified official V15
  if (!confirm("¿Instalar el firmware oficial V15? No desconectes el FM-1 hasta que la página diga «Hecho».")) return;
  await new Updater(access, log).install(stock.image, stock.product, progress(stock.image.length));
});

$("connect").onclick = connect;
$("stockfile").onchange = refresh;
document.querySelectorAll(".pre").forEach((c) => (c.onchange = refresh));
loadPackage();
