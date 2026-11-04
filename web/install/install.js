// SPDX-License-Identifier: GPL-3.0-only
// MEL's web installer: Felucca's update protocol (fm1ota.js, fm1pkg.js) with MEL's checks on top, the same as
// tools/fm1_install.py: the package is whole and is MEL, for the keyboard chosen. What the keyboard runs is never a
// gate, nor a warning (step 2's advice says it once): Install installs. An install
// cut off is finished, never started over; the official firmware can always go back on. Two boards: the FM-1, and the SMK-37 (Pro, Elite, MKE-P37: one keyboard,
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
    pkgOk: () => "Check passed.",
    pkgBad: () => "Corrupt file.",
    pkgNone: () => "Not available for this model yet.",
    shaBad: () => "the file does not match its fingerprint (SHA-256): reload the page",
    idBad: (p) => `unexpected identity: ${p}`,
    devNone: () => "Not connected.",
    noMidi: () => "this browser has no Web MIDI: use Chrome or Edge",
    denied: () => "Allow MIDI on this page.",
    cantMidi: () => "Use Chrome or Edge on a computer.",
    percent: (n) => `Writing ${n} %… don't unplug it.`,
    copied: () => "Log copied.",
    looking: () => "Looking…",
    notFound: () => "Not found.",
    cutOff: () => "Unfinished install: finish it.",
    otherLoader: (t) => `The FM-1 is in another firmware's update mode (${t}): finish that update with its own updater (M-UPGRADE), then come back.`,
    isMel: (t) => `FMEL-1 (${t}): ready to update.`,
    rescue: () => "In rescue: ready.",
    official: (t) => `Official firmware (${t}): ready.`,
    found: (t) => `Found: ${t}. Ready.`,
    wrongBoard: (t) => `That is ${t}, not the keyboard chosen at the top: choose the other one there.`,
    error: (m) => `Error: ${m}`,
    start: (t) => `Starting on ${t}…`,
    verify: () => "Checking…",
    loader: () => "Update mode… don't unplug it.",
    write: () => "Writing… don't unplug it.",
    reboot: () => "Restarting…",
    done: () => "Done: you can unplug it.",
    askMel: (m) => `Install FMEL-1 for the ${m}? Don't unplug it until the page says «Done».`,
    askStock: () => "Install the official V15 firmware? Don't unplug the FM-1 until the page says «Done».",
    notReady: () => "The FM-1 is no longer ready to install: look for it again.",
    noResume: () => "Could not finish: unplug it, plug it in again and look for the FM-1 again.",
    needFind: () => "Missing: find the keyboard.",
    needFile: () => "Missing: choose the file.",
    needPkg: () => "Missing: the package.",
    inLoader: () => "Missing: finish the unfinished install.",
  },
  es: {
    pkgChecking: () => "Comprobando el paquete…",
    pkgOk: () => "Comprobación correcta.",
    pkgBad: () => "Fichero corrupto.",
    pkgNone: () => "Aún no hay versión para este modelo.",
    shaBad: () => "el archivo no coincide con su huella (SHA-256): recarga la página",
    idBad: (p) => `identidad inesperada: ${p}`,
    devNone: () => "Sin conectar.",
    noMidi: () => "este navegador no tiene Web MIDI: usa Chrome o Edge",
    denied: () => "Permite MIDI en esta página.",
    cantMidi: () => "Usa Chrome o Edge en un ordenador.",
    percent: (n) => `Escribiendo ${n} %… no lo desconectes.`,
    copied: () => "Log copiado.",
    looking: () => "Buscando…",
    notFound: () => "No se encuentra.",
    cutOff: () => "Instalación a medias: termínala.",
    otherLoader: (t) => `El FM-1 está en modo actualización de otro firmware (${t}): termina esa actualización con su propio instalador (M-UPGRADE) y vuelve.`,
    isMel: (t) => `FMEL-1 (${t}): listo para actualizar.`,
    rescue: () => "En rescate: listo.",
    official: (t) => `Firmware oficial (${t}): listo.`,
    found: (t) => `Encontrado: ${t}. Listo.`,
    wrongBoard: (t) => `Eso es un ${t}, no el teclado elegido arriba: elige el otro ahí.`,
    error: (m) => `Error: ${m}`,
    start: (t) => `Empezando en ${t}…`,
    verify: () => "Comprobando…",
    loader: () => "Modo actualización… no lo desconectes.",
    write: () => "Escribiendo… no lo desconectes.",
    reboot: () => "Reiniciando…",
    done: () => "Hecho: ya puedes desconectarlo.",
    askMel: (m) => `¿Instalar FMEL-1 para el ${m}? No lo desconectes hasta que la página diga «Hecho».`,
    askStock: () => "¿Instalar el firmware oficial V15? No desconectes el FM-1 hasta que la página diga «Hecho».",
    notReady: () => "El FM-1 ya no está listo para instalar: búscalo otra vez.",
    noResume: () => "No he podido terminar: desconecta, vuelve a conectar y busca el FM-1 otra vez.",
    needFind: () => "Falta: encontrar el teclado.",
    needFile: () => "Falta: elegir el archivo.",
    needPkg: () => "Falta: el paquete.",
    inLoader: () => "Falta: terminar la instalación a medias.",
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
const installable = (id) => onBoard(id) && !isLoader(id);   // (whatever it runs: the advice is said, the reader decides)

// ---- the package: whole (its CRCs, its SHA-256 as published), and MEL for the chosen keyboard
async function loadMeta() {
  try {
    meta = await (await fetch("mel.json", { cache: "no-store" })).json();
    if (!meta.packages && meta.file) meta.packages = { fm1: { file: meta.file, sha256: meta.sha256, product: meta.product } };
  } catch (e) {
    meta = null;
  }
  $("ver").textContent = meta && meta.version ? meta.version.replace(/^v/, "") : "";   // what Install installs
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
    say("pkg", "pkgOk", "ok");
    log(`package ${p.file} ${pkg.product} ${meta.version} sha256 ${p.sha256}`);
  } catch (e) {
    mel = null;
    say("pkg", "pkgBad", "bad", e.message);
  }
  refresh();
}

function refresh() {
  const ready = mel && device && !busy;
  $("install").disabled = !(ready && installable(device));
  $("resume").hidden = !(device && onBoard(device) && isOurLoader(device));
  $("resume").disabled = !(mel && device && !busy);
  $("connect").disabled = busy;
  $("stock").disabled = busy || !$("stockfile").files.length || !device || isLoader(device) || !isFm1(device);
  document.querySelectorAll("[name=board], [name=model]").forEach((r) => (r.disabled = busy));
  // what each disabled button still waits for, the first thing missing
  const why = (el, keys) => { const k = busy ? null : keys.find(Boolean); if (k) say(el, k, "muted"); else { delete shown[el]; $(el).textContent = ""; } };
  why("installwhy", [!device && "needFind", !mel && "needPkg", device && isLoader(device) && "inLoader"]);
  why("stockwhy", [!device && "needFind", device && isLoader(device) && "inLoader",
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
    else if (isOfficialOrRescue(device)) say("dev", "official", "ok", device.text);
    else say("dev", "found", "ok", device.text);   // (another firmware: found, and ready; no warning)
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
if (!navigator.requestMIDIAccess) say("mine", "cantMidi", "bad");   // (only when it can't: nothing to say otherwise)
$("copylog").onclick = async () => {
  try { await navigator.clipboard.writeText($("log").textContent); say("status", "copied", "ok"); } catch (e) {}
};
say("pkg", "pkgChecking");
say("dev", "devNone", "muted");
showBoard();
$("connect").onclick = connect;
$("stockfile").onchange = refresh;
loadMeta();
