// SPDX-License-Identifier: GPL-3.0-only
// MEL's web installer: Felucca's update protocol (fm1ota.js, fm1pkg.js) with MEL's checks on top, the same as
// tools/fm1_install.py: the package is whole and is MEL; the keyboard runs M-VAVE's own firmware, the USB rescue or
// MEL (never straight over another custom firmware); an install cut off is finished, never started over; the
// official firmware can always go back on. Two boards: the FM-1, and the SMK-37 (Pro, Elite, MKE-P37: one keyboard,
// three names, each with its own package so that M-VAVE's updater still takes the unit's own factory one back).
import { Updater, sameKeyboard } from "./fm1ota.js";
import { Package, logicalImage, sha256hex, validateStockPackage } from "./fm1pkg.js";

const $ = (id) => document.getElementById(id);
let lang = document.documentElement.lang === "es" ? "es" : "en";
const MODELS = { pro: "SMK-37 Pro", elite: "SMK-37 Elite", mke: "MKE-P37" };
let board = "fm1", model = "pro";
try {
  const q = new URLSearchParams(location.search).get("board"), b = q || localStorage.getItem("mel-board");
  if (b === "smk37") board = "smk37";
  const m = localStorage.getItem("mel-smk-model");
  if (m in MODELS) model = m;
} catch (e) {}
const D = () => (board === "fm1" ? "FM-1" : "SMK-37");   // the keyboard, as the page names it
const pkgKey = () => (board === "fm1" ? "fm1" : "smk37-" + model);

const M = {                                     // what the page says, by language ("FM-1" and "V15": the board's)
  en: {
    pkgChecking: () => "Checking the package…",
    pkgOk: (v, p, kb) => `FMEL-1 ${v} (${p}), ${kb} KB: checked.`,
    pkgBad: (why) => `The package cannot be used: ${why}. Nothing will be installed.`,
    pkgNone: (m) => `There is no FMEL-1 package for the ${m} yet.`,
    shaBad: () => "the file does not match its fingerprint (SHA-256): reload the page",
    idBad: (p) => `unexpected identity: ${p}`,
    devNone: () => "Not connected.",
    noMidi: () => "this browser has no Web MIDI: use Chrome or Edge",
    denied: () => "The browser did not let this page use MIDI. Allow «MIDI devices» for this page (the lock left of the address), reload, and look again.",
    canMidi: () => "This browser can install FMEL-1.",
    cantMidi: () => "This browser can't: open this page in Chrome or Edge on a computer.",
    percent: (n) => `Writing… ${n} %. Don't unplug it.`,
    copied: () => "Log copied.",
    looking: () => "Looking…",
    notFound: () => "The FM-1 is not found: a data cable? another app using it? Unplug it, plug it in again and try again.",
    cutOff: (t) => `The FM-1 is in update mode (${t}): an install was cut off. Press «Finish the install».`,
    otherLoader: (t) => `The FM-1 is in another firmware's update mode (${t}): finish that update with its own updater (M-UPGRADE), then come back.`,
    isMel: (t) => `The FM-1 runs FMEL-1 (${t}): it can be updated.`,
    rescue: () => "The FM-1 is in the USB rescue: you can install.",
    official: (t) => `The FM-1 runs the official firmware (${t}): you can install.`,
    oldStock: (t) => `The SMK-37 runs the official firmware ${t}: update it to V016 with M-UPGRADE first (step 5), then install FMEL-1.`,
    custom: (t) => `The FM-1 runs another unofficial firmware (${t}). Go back to the official V15 first (step 5), then install FMEL-1.`,
    wrongBoard: (t) => `That is ${t}, not the keyboard chosen at the top: choose the other one there.`,
    error: (m) => `Error: ${m}`,
    start: (t) => `Starting on ${t}…`,
    verify: (n) => `The FM-1 checks the package (${n})…`,
    loader: () => "Switching to update mode… don't unplug it.",
    write: () => "Writing… don't unplug it.",
    reboot: () => "Restarting…",
    done: (t) => `Done: the FM-1 runs ${t}. You can unplug it now.`,
    askMel: (m) => `Install FMEL-1 for the ${m}? Don't unplug it until the page says «Done».`,
    askStock: () => "Install the official V15 firmware? Don't unplug the FM-1 until the page says «Done».",
    notReady: () => "The FM-1 is no longer ready to install: look for it again.",
    noResume: () => "Could not finish: unplug it, plug it in again and look for the FM-1 again.",
    needAck: () => "To enable the button: tick the box in step 2.",
    needFind: () => "To enable the button: press «Find my FM-1» (step 3).",
    needFile: () => "To enable the button: choose FM-1.fwsc.",
    needPkg: () => "The FMEL-1 package is not ready (step 3).",
    inLoader: () => "The FM-1 is in update mode: finish that first (step 3).",
    cantHere: () => "Not over what the FM-1 runs now (step 3 says why).",
  },
  es: {
    pkgChecking: () => "Comprobando el paquete…",
    pkgOk: (v, p, kb) => `FMEL-1 ${v} (${p}), ${kb} KB: comprobado.`,
    pkgBad: (why) => `No se puede usar el paquete: ${why}. No se instalará nada.`,
    pkgNone: (m) => `Todavía no hay paquete de FMEL-1 para el ${m}.`,
    shaBad: () => "el archivo no coincide con su huella (SHA-256): recarga la página",
    idBad: (p) => `identidad inesperada: ${p}`,
    devNone: () => "Sin conectar.",
    noMidi: () => "este navegador no tiene Web MIDI: usa Chrome o Edge",
    denied: () => "El navegador no ha dejado a esta página usar MIDI. Permite «Dispositivos MIDI» para esta página (el candado a la izquierda de la dirección), recarga y busca otra vez.",
    canMidi: () => "Este navegador puede instalar FMEL-1.",
    cantMidi: () => "Este navegador no puede: abre esta página en Chrome o Edge en un ordenador.",
    percent: (n) => `Escribiendo… ${n} %. No lo desconectes.`,
    copied: () => "Registro copiado.",
    looking: () => "Buscando…",
    notFound: () => "No encuentro el FM-1: ¿cable de datos?, ¿otra app lo usa? Desconecta, vuelve a conectar y prueba otra vez.",
    cutOff: (t) => `El FM-1 está en modo actualización (${t}): una instalación se cortó. Pulsa «Terminar la instalación».`,
    otherLoader: (t) => `El FM-1 está en modo actualización de otro firmware (${t}): termina esa actualización con su propio instalador (M-UPGRADE) y vuelve.`,
    isMel: (t) => `El FM-1 lleva FMEL-1 (${t}): se puede actualizar.`,
    rescue: () => "El FM-1 está en el rescate USB: se puede instalar.",
    official: (t) => `El FM-1 lleva el firmware oficial (${t}): se puede instalar.`,
    oldStock: (t) => `El SMK-37 lleva el firmware oficial ${t}: primero actualízalo al V016 con M-UPGRADE (paso 5) y después instala FMEL-1.`,
    custom: (t) => `El FM-1 lleva otro firmware no oficial (${t}). Primero vuelve al V15 oficial (paso 5) y después instala FMEL-1.`,
    wrongBoard: (t) => `Eso es un ${t}, no el teclado elegido arriba: elige el otro ahí.`,
    error: (m) => `Error: ${m}`,
    start: (t) => `Empezando en ${t}…`,
    verify: (n) => `El FM-1 comprueba el paquete (${n})…`,
    loader: () => "Pasando a modo actualización… no lo desconectes.",
    write: () => "Escribiendo… no lo desconectes.",
    reboot: () => "Reiniciando…",
    done: (t) => `Hecho: el FM-1 lleva ${t}. Ya puedes desconectarlo.`,
    askMel: (m) => `¿Instalar FMEL-1 para el ${m}? No lo desconectes hasta que la página diga «Hecho».`,
    askStock: () => "¿Instalar el firmware oficial V15? No desconectes el FM-1 hasta que la página diga «Hecho».",
    notReady: () => "El FM-1 ya no está listo para instalar: búscalo otra vez.",
    noResume: () => "No he podido terminar: desconecta, vuelve a conectar y busca el FM-1 otra vez.",
    needAck: () => "Para activar el botón: marca la casilla del paso 2.",
    needFind: () => "Para activar el botón: pulsa «Buscar mi FM-1» (paso 3).",
    needFile: () => "Para activar el botón: elige FM-1.fwsc.",
    needPkg: () => "El paquete de FMEL-1 no está listo (paso 3).",
    inLoader: () => "El FM-1 está en modo actualización: termina eso primero (paso 3).",
    cantHere: () => "No encima de lo que lleva ahora el FM-1 (el paso 3 dice por qué).",
  },
};
const tr = (key, ...a) => {
  const s = M[lang][key](...a);
  return board === "fm1" ? s : s.replace(/FM-1(?![._])/g, "SMK-37").replace(/\bV15\b/g, "V016");
};
const shown = {};                               // what each message element says now, to say it again in the other language
const say = (el, key, cls = "", ...a) => { shown[el] = [key, cls, a]; $(el).textContent = tr(key, ...a); $(el).className = cls; };
class Say extends Error { constructor(key, ...a) { super(tr(key, ...a)); this.key = key; this.args = a; } }
const log = (t) => { $("log").textContent += t + "\n"; $("log").scrollTop = 1e9; };

let meta = null;           // mel.json: {version, packages: {fm1, smk37-pro, smk37-elite, smk37-mke: {file, sha256, product}}}
let mel = null;            // the chosen keyboard's package: {product, image, version}
let access = null;
let busy = false;
let device = null;         // the identity found: {text, model, version}

// MEL is FM-1_95N on the FM-1, <model>_45N on the SMK-37; M-VAVE's own firmware is _0NN (_000: the USB rescue of a
// Felucca-family firmware); the update loaders are ota-<model>_NNN
const isFm1 = (id) => /^(ota-)?FM-1$/i.test(id.model);
const plainModel = (id) => id.model.replace(/^ota-/i, "");
const onBoard = (id) => (board === "fm1" ? isFm1(id) : !isFm1(id) && sameKeyboard(plainModel(id), MODELS.pro));
const isMel = (id) => (board === "fm1" ? /^FM-1_95\d$/ : /_45\d$/).test(id.text) && !isLoader(id);
const isOfficialOrRescue = (id) => /_0\d\d$/.test(id.text) && !isLoader(id);
const isLoader = (id) => /^ota-/i.test(id.model);
const isOurLoader = (id) => /^ota-.+_[49]\d\d$/i.test(id.text);   // Felucca's loader, as MEL's packages carry
const stockTooOld = (id) => board === "smk37" && /_0\d\d$/.test(id.text) && id.version > 0 && id.version < 16;
const installable = (id) => onBoard(id) && (isMel(id) || (isOfficialOrRescue(id) && !stockTooOld(id)));

// ---- the package: whole (its CRCs, its SHA-256 as published), and MEL for the chosen keyboard
async function loadMeta() {
  try {
    meta = await (await fetch("mel.json", { cache: "no-store" })).json();
    if (!meta.packages && meta.file) meta.packages = { fm1: { file: meta.file, sha256: meta.sha256, product: meta.product } };
  } catch (e) {
    meta = null;
  }
  await loadPackage();
}
async function loadPackage() {
  mel = null;
  const p = meta && meta.packages && meta.packages[pkgKey()];
  const name = board === "fm1" ? "FM-1" : MODELS[model];
  if (!meta) { say("pkg", "pkgBad", "bad", "mel.json"); return refresh(); }
  if (!p) { say("pkg", "pkgNone", "bad", name); return refresh(); }
  say("pkg", "pkgChecking");
  try {
    const bytes = new Uint8Array(await (await fetch(p.file, { cache: "no-store" })).arrayBuffer());
    if (await sha256hex(bytes) !== p.sha256) throw new Say("shaBad");
    const pkg = new Package(bytes);                       // throws on any CRC mismatch
    const want = board === "fm1" ? /^FM-1_95\d$/ : new RegExp(`^${MODELS[model]}_45\\d$`);
    if (!want.test(pkg.product) || pkg.product !== p.product) throw new Say("idBad", pkg.product);
    mel = { product: pkg.product, image: logicalImage(bytes), version: meta.version };
    say("pkg", "pkgOk", "ok", meta.version, pkg.product, Math.round(bytes.length / 1024));
    log(`package ${p.file} sha256 ${p.sha256}`);
  } catch (e) {
    mel = null;
    say("pkg", "pkgBad", "bad", e.message);
  }
  refresh();
}

function prechecked() { return [...document.querySelectorAll(".pre")].every((c) => c.checked); }

function refresh() {
  const ready = mel && device && !busy && prechecked();
  $("install").disabled = !(ready && installable(device));
  $("resume").hidden = !(device && onBoard(device) && isOurLoader(device));
  $("resume").disabled = !(mel && device && !busy && prechecked());
  $("connect").disabled = busy;
  $("stock").disabled = busy || !$("stockfile").files.length || !prechecked() || !device || isLoader(device) || !isFm1(device);
  document.querySelectorAll("[name=board], [name=model]").forEach((r) => (r.disabled = busy));
  // what each disabled button still waits for, the first thing missing
  const why = (el, keys) => { const k = busy ? null : keys.find(Boolean); if (k) say(el, k, "muted"); else { delete shown[el]; $(el).textContent = ""; } };
  why("installwhy", [!prechecked() && "needAck", !device && "needFind", !mel && "needPkg", device && isLoader(device) && "inLoader",
                     device && !installable(device) && "cantHere"]);
  why("stockwhy", [!prechecked() && "needAck", !device && "needFind", device && isLoader(device) && "inLoader",
                   !$("stockfile").files.length && "needFile"]);
}

// ---- the keyboard: what it runs decides what may happen
async function connect() {
  try {
    if (!navigator.requestMIDIAccess) throw new Say("noMidi");
    try { access = access || await navigator.requestMIDIAccess({ sysex: true }); }
    catch (e) { if (e && (e.name === "SecurityError" || e.name === "NotAllowedError")) throw new Say("denied"); throw e; }
    say("dev", "looking", "muted");
    const dev = await new Updater(access, log).find();
    if (!dev) { device = null; say("dev", "notFound", "bad"); return refresh(); }
    dev.link.close();
    device = dev.id;
    log(`device ${device.text} on ${dev.name}`);
    if (!onBoard(device)) say("dev", "wrongBoard", "bad", isFm1(device) ? "FM-1" : "SMK-37");
    else if (isLoader(device) && isOurLoader(device)) say("dev", "cutOff", "bad", device.text);
    else if (isLoader(device)) say("dev", "otherLoader", "bad", device.text);
    else if (isMel(device)) say("dev", "isMel", "ok", device.text);
    else if (/_000$/.test(device.text)) say("dev", "rescue", "ok");
    else if (stockTooOld(device)) say("dev", "oldStock", "bad", device.text);
    else if (isOfficialOrRescue(device)) say("dev", "official", "ok", device.text);
    else say("dev", "custom", "bad", device.text);
  } catch (e) {
    say("dev", "error", "bad", e.message);
  }
  refresh();
}

function progress(total) {
  return (k, n) => {
    if (k === "start") say("status", "start", "", n);
    else if (k === "verify") say("status", "verify", "", n);
    else if (k === "loader") say("status", "loader");
    else if (k === "write") {
      const pc = Math.min(99, Math.round(n * 512 * 100 / total));
      $("bar").hidden = false; $("bar").value = pc; $("bar").setAttribute("aria-valuetext", pc + " %");
      say("status", "percent", "", pc);
    }
    else if (k === "reboot") say("status", "reboot");
    else if (k === "done") { $("bar").value = 100; say("status", "done", "ok", n); }
    log(`${k} ${n ?? ""}`);
  };
}

async function guarded(work) {
  busy = true; refresh();
  const warn = (e) => { e.preventDefault(); e.returnValue = ""; };
  window.addEventListener("beforeunload", warn);
  let wake = null;                                // (the screen stays on while it writes)
  try { wake = navigator.wakeLock ? await navigator.wakeLock.request("screen") : null; } catch (e) {}
  try { await work(); }
  catch (e) {
    if (e.key) say("status", e.key, "bad", ...e.args);
    else say("status", "error", "bad", e.message);   // (the protocol's own messages are in English)
    log(`error ${e.code || ""} ${e.message}`);
  } finally {
    window.removeEventListener("beforeunload", warn);
    try { if (wake) await wake.release(); } catch (e) {}
    busy = false; device = null; refresh();
  }
}

$("install").onclick = () => guarded(async () => {
  if (!confirm(tr("askMel", board === "fm1" ? "FM-1" : MODELS[model]))) return;
  await connect();                                           // what it runs now, not when the page looked
  if (!device || !installable(device)) throw new Say("notReady");
  await new Updater(access, log).install(mel.image, mel.product, progress(mel.image.length));
});

$("resume").onclick = () => guarded(async () => {
  const ok = await new Updater(access, log).resume(mel.image, progress(mel.image.length), mel.product);
  if (!ok) throw new Say("noResume");
});

$("stock").onclick = () => guarded(async () => {
  const bytes = new Uint8Array(await $("stockfile").files[0].arrayBuffer());
  const stock = await validateStockPackage(bytes);           // only the unmodified official V15
  if (!confirm(tr("askStock"))) return;
  await new Updater(access, log).install(stock.image, stock.product, progress(stock.image.length));
});

// ---- the board and the model: the page, the package
function showBoard() {
  document.documentElement.dataset.board = board;
  document.querySelectorAll(".dn").forEach((e) => (e.textContent = D()));
  document.querySelectorAll("[name=board]").forEach((r) => (r.checked = r.value === board));
  document.querySelectorAll("[name=model]").forEach((r) => (r.checked = r.value === model));
  $("sim").href = board === "fm1" ? "../" : "../smk37.html";
  document.title = (lang === "es" ? "Instalar FMEL-1 en el " : "Install FMEL-1 on the ") + D();
  for (const [el, [key, cls, a]] of Object.entries(shown)) say(el, key, cls, ...a);
}
document.querySelectorAll("[name=board]").forEach((r) => (r.onchange = () => {
  board = r.value;
  try { localStorage.setItem("mel-board", board); history.replaceState(null, "", board === "fm1" ? "./" : "./?board=smk37"); } catch (e) {}
  device = null;
  say("dev", "devNone", "muted");
  showBoard();
  loadPackage();
}));
document.querySelectorAll("[name=model]").forEach((r) => (r.onchange = () => {
  model = r.value;
  try { localStorage.setItem("mel-smk-model", model); } catch (e) {}
  showBoard();
  loadPackage();
}));

$("lang").onclick = () => {
  lang = lang === "es" ? "en" : "es";
  document.documentElement.lang = lang;
  try { localStorage.setItem("mel-lang", lang); } catch (e) {}
  showBoard();
};
say("mine", navigator.requestMIDIAccess ? "canMidi" : "cantMidi", navigator.requestMIDIAccess ? "ok" : "bad");
$("copylog").onclick = async () => {
  try { await navigator.clipboard.writeText($("log").textContent); say("mine", "copied", "ok"); } catch (e) {}
};
say("pkg", "pkgChecking");
say("dev", "devNone", "muted");
showBoard();
$("connect").onclick = connect;
$("stockfile").onchange = refresh;
document.querySelectorAll(".pre").forEach((c) => (c.onchange = refresh));
loadMeta();
