// ARMOR-ELECTRICAL - the web panel in a real browser (headless Microsoft Edge) against the stand-in node of tools/panel_mock.mjs: set-up, login, the pages, saving with a refused and a
// corrected value, every page in the seven languages, the width of a phone. Development tool; it needs Edge, and `ws` from ARMOR-SERVER's node_modules.
//   node tools/panel_browser_test.mjs [s3-wifi|s3-eth]
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
const HERE = path.dirname(fileURLToPath(import.meta.url));
const R = path.resolve(HERE, "..", "..");   // the folder that holds the ARMOR repositories
const require = createRequire(R + "/ARMOR-SERVER/package.json");
const WebSocket = require("ws");
const OUT = path.join(os.tmpdir(), "armor-panel-shots");   // where the screenshots go
fs.mkdirSync(OUT, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "armor-epanel-"));
const board = process.argv[2] ?? "s3-wifi";
const mock = spawn("node", ["tools/panel_mock.mjs", "18122", "--user", "admin:adminpass123", "--board", board], { cwd: R + "/ARMOR-ELECTRICAL", stdio: "ignore" });
const fresh = spawn("node", ["tools/panel_mock.mjs", "18123", "--board", board], { cwd: R + "/ARMOR-ELECTRICAL", stdio: "ignore" });
let edge;
const cleanup = () => { for (const p of [mock, fresh, edge]) try { p?.kill(); } catch { /* ignore */ } };
process.on("exit", cleanup);
const results = [];
const check = (name, ok, extra = "") => { results.push(ok); console.log(ok ? "PASS" : "FAIL", name, ok ? "" : extra); };
try {
  await sleep(1500);
  const login = await fetch("http://127.0.0.1:18122/api/v1/login", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ user: "admin", password: "adminpass123" }) });
  const cookie = login.headers.getSetCookie()[0].split(";")[0];
  const [cname, cvalue] = [cookie.slice(0, cookie.indexOf("=")), cookie.slice(cookie.indexOf("=") + 1)];
  edge = spawn("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe", ["--headless=new", "--remote-debugging-port=9366", "--user-data-dir=" + path.join(tmp, "edge"), "--no-first-run", "--disable-gpu", "--window-size=1300,900", "about:blank"], { stdio: "ignore" });
  let targets;
  for (let i = 0; i < 40; i += 1) { try { targets = await (await fetch("http://127.0.0.1:9366/json")).json(); if (targets.length) break; } catch { /* wait */ } await sleep(300); }
  const page = targets.find(t => t.type === "page");
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => ws.on("open", r));
  let id = 0; const waiting = new Map(); const errors = [];
  ws.on("message", raw => {
    const m = JSON.parse(raw);
    if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m.result ?? m.error); waiting.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") errors.push(m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
    if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") errors.push(m.params.args.map(a => a.value ?? a.description).join(" "));
    if (m.method === "Log.entryAdded" && m.params.entry.level === "error") errors.push(m.params.entry.text + " " + (m.params.entry.url ?? ""));
  });
  const send = (method, params = {}) => new Promise(r => { id += 1; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
  await send("Page.enable"); await send("Network.enable"); await send("Runtime.enable"); await send("Log.enable");
  await send("Emulation.setDeviceMetricsOverride", { width: 1300, height: 900, deviceScaleFactor: 1, mobile: false });
  const ev = async expr => { const r = await send("Runtime.evaluate", { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) console.log("EXC", r.exceptionDetails.exception?.description ?? r.exceptionDetails.text); return r.result?.value; };
  const shot = async file => { const r = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: true }); fs.writeFileSync(path.join(OUT, file), Buffer.from(r.data, "base64")); };
  const text = () => ev("document.body.innerText");
  const goto = async hash => { await ev(`location.hash = '#/${hash}'`); await sleep(900); };
  const setText = (selector, value) => ev(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); if (!e) return false; e.focus(); e.value = ${JSON.stringify(value)}; e.dispatchEvent(new Event('input', { bubbles: true })); return true; })()`);
  const clickButton = label => ev(`(() => { const b = [...document.querySelectorAll('button')].find(x => x.textContent.trim() === ${JSON.stringify(label)}); if (!b) return false; b.click(); return true; })()`);

  await send("Network.setCookie", { name: cname, value: cvalue, url: "http://127.0.0.1:18122/", path: "/" });
  await send("Page.navigate", { url: "http://127.0.0.1:18122/" });
  await sleep(1800);

  // ---- overview
  let body = await text();
  check("the overview shows the node and the meters card", body.includes("This node") && body.includes("Meters"));
  check("the overview lists the enabled meters with their readings", body.includes("Grid input") && body.includes("PZEM-004T v3") && body.includes("PZEM-017") && /\d+ W/.test(body));
  check("the overview has no solar leftovers", !/inverter|battery|Pylontech|ports/i.test(body.replace(/Battery bus/g, "")), body.match(/inverter|Pylontech|ports/i)?.[0]);
  await shot(`epanel-${board}-overview.png`);

  // ---- the menu
  body = await text();
  const menuLines = body.split(/\r?\n/).map(s => s.trim());
  check("the menu has Meters and Readings and no Ports", menuLines.includes("Meters") && menuLines.includes("Readings") && !menuLines.includes("Ports"), menuLines.slice(0, 20).join("|"));

  // ---- the meters page
  await goto("meters");
  body = await text();
  check("the meters page has the serial line and sixteen meters", body.includes("Serial line") && [1, 2, 8, 16].every(n => body.includes(`Meter ${n}`)));
  check("the warning about mains voltage and the restart note are shown", body.includes("mains voltage") && body.includes("restart of the node"));
  check("meter states are shown with their hints", body.includes("reading") && body.includes("silent") && body.includes("The meter does not answer"));
  const selects = await ev("document.querySelectorAll('select').length");
  check("the line has its pin and speed selects", selects >= 3, String(selects));
  check("the second serial line and the driver-enable pin are offered", body.includes("Second serial line") && body.includes("Driver-enable pin (RS485)") && body.includes("RX pin of line 2"));
  check("each meter says which line it is on", (body.match(/Line 1/g) ?? []).length >= 4);
  await shot(`epanel-${board}-meters.png`);

  // the live counters change on their own without losing what is typed
  await setText("input[type=text]", "typed-and-kept");
  const before = await ev("document.getElementById('meter-live-0')?.textContent");
  await sleep(4500);
  const after = await ev("document.getElementById('meter-live-0')?.textContent");
  const kept = await ev("document.querySelector('input[type=text]').value");
  check("the counters refresh by themselves", before && after && before !== after, `${before} -> ${after}`);
  check("what is being typed is not lost when the counters refresh", kept === "typed-and-kept", kept);
  await ev("document.querySelector('button.b:not(.primary):not(.danger)')?.textContent");
  await clickButton("Discard changes");
  await sleep(400);

  // enable meter 6, give it a channel that already exists: the node's answer is shown on the field
  const checks = await ev("[...document.querySelectorAll('label.check input')].length");
  check("each meter has its enable box", checks >= 16, String(checks));
  await ev(`(() => { const cards = [...document.querySelectorAll('section.card')]; const c = cards.find(x => x.querySelector('h2')?.textContent.startsWith('Meter 6')); const box = c.querySelector('input[type=checkbox]'); box.checked = true; box.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await sleep(500);
  const card6 = await ev("(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('Meter 6')); return c ? c.innerText : ''; })()");
  check("enabling a meter shows its fields", card6.includes("Channel identifier") && card6.includes("Modbus address") && card6.includes("PZEM-004T v3"), card6);
  await ev(`(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('Meter 6')); const ins = c.querySelectorAll('input[type=text]'); ins[0].value = 'grid'; ins[0].dispatchEvent(new Event('input', { bubbles: true })); })()`);
  await sleep(300);
  check("the save bar appears once something changed", (await text()).includes("Save"));
  await clickButton("Save"); await sleep(900);
  body = await text();
  check("a repeated channel is refused by the node and the panel says so", /already|conflict|used|twice|repeat/i.test(body) || (await ev("document.querySelectorAll('.err, input.bad').length")) > 0, body.slice(0, 200));
  await shot(`epanel-${board}-meters-problem.png`);
  // fix it and save
  await ev(`(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('Meter 6')); const ins = c.querySelectorAll('input[type=text]'); ins[0].value = 'garage'; ins[0].dispatchEvent(new Event('input', { bubbles: true })); })()`);
  await clickButton("Save"); await sleep(900);
  const saved = await (await fetch("http://127.0.0.1:18122/api/v1/config", { headers: { Cookie: cookie } })).json();
  check("the corrected meter is saved on the node", saved.config.meters[5].enabled === true && saved.config.meters[5].channel === "garage", JSON.stringify(saved.config.meters[5]));
  check("saving the meters says a restart is needed", (await text()).toLowerCase().includes("restart"));

  // the line: change the speed and the poll period, save
  await goto("meters");
  await ev(`(() => { const sel = [...document.querySelectorAll('select')].find(s => [...s.options].some(o => o.value === '19200')); sel.value = '19200'; sel.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await clickButton("Save"); await sleep(800);
  const saved2 = await (await fetch("http://127.0.0.1:18122/api/v1/config", { headers: { Cookie: cookie } })).json();
  check("the line's speed is saved as a number", saved2.config.bus.baud === 19200, JSON.stringify(saved2.config.bus));

  // ---- the readings page
  await goto("readings");
  body = await text();
  check("the readings page shows a card per channel with values", body.includes("Grid input") && body.includes("231.4 V") && body.includes("Frequency") && body.includes("Power factor") && body.includes("kWh"), body.slice(0, 300));
  check("the DC channel has no frequency of its own", (await ev("[...document.querySelectorAll('section.card')].find(c => c.innerText.includes('Battery bus'))?.innerText.includes('Frequency')")) === false);
  await shot(`epanel-${board}-readings.png`);

  // ---- the Zigbee radio page
  await ev("window.confirm = () => true");
  await goto("zigbee");
  body = await text();
  check("the Zigbee page has the radio card, the warning and the Zigbee2MQTT lines", body.includes("Zigbee radio") && body.includes("only to the one address") && body.includes("tcp://192.168.0.181:8888") && body.includes("adapter: zstack"), body.slice(0, 300));
  check("the bridge shows it is connected with its counters", body.includes("connected") && /\d+ bytes to the radio/.test(body) && body.includes("Connected from 192.168.0.180"), body.slice(0, 400));
  check("the radio has its reset and boot-loader buttons for an administrator", body.includes("Reset the radio") && body.includes("Radio to boot loader"));
  await clickButton("Reset the radio"); await sleep(700);
  check("resetting the radio is answered", !(await text()).includes("not available"), (await text()).slice(0, 200));
  await shot(`epanel-${board}-zigbee.png`);
  // the allowed client is required: an empty one is refused and said so
  await setText("input[placeholder='192.168.0.180']", "");
  await clickButton("Save"); await sleep(900);
  check("an empty allowed client is refused and the panel says so", (await text()).includes("required") && (await ev("document.querySelectorAll('input.bad').length")) > 0);
  await setText("input[placeholder='192.168.0.180']", "192.168.0.50");
  await clickButton("Save"); await sleep(900);
  const savedZ = await (await fetch("http://127.0.0.1:18122/api/v1/config", { headers: { Cookie: cookie } })).json();
  check("the corrected radio settings are saved", savedZ.config.zigbee.allowed_client === "192.168.0.50" && savedZ.config.zigbee.baud === 115200 && savedZ.config.zigbee.port === 8888, JSON.stringify(savedZ.config.zigbee));

  // ---- the inputs and outputs page
  await goto("io");
  body = await text();
  check("the page has eight relays and eight analog inputs", [1, 2, 8].every(n => body.includes(`Relay ${n}`)) && [1, 2, 8].every(n => body.includes(`Input ${n}`)) && body.includes("Relay outputs") && body.includes("Analog inputs"), body.slice(0, 300));
  check("the direct API for a central is offered with its token", body.includes("Let a central read and command this node over HTTP"));
  await ev(`(() => { const box = [...document.querySelectorAll('label')].find(l => l.textContent.includes('Let a central read')); box.querySelector('input').click(); })()`);
  await sleep(300);
  check("turning it on shows how to use the token and a button to make one", (await text()).includes("Authorization: Bearer") && (await text()).includes("Generate a new token"));
  await ev(`document.getElementById('remote-token-generate').click()`);
  await sleep(300);
  const shownToken = await ev(`document.getElementById('remote-token-shown') ? document.getElementById('remote-token-shown').textContent : ''`);
  check("a generated token is long, shown once and put in the settings", /^[0-9a-f]{48}$/.test(shownToken) && (await text()).includes("Copy it now"), shownToken);
  check("the warnings about the relays and about 3.3 V are shown", body.includes("starts open when the node starts") && body.includes("more than 3.3 V"));
  check("the analog inputs show what the pin reads", body.includes("mV at the pin") && body.includes("21.213 A"), body.slice(0, 400));
  check("a running relay shows its state and a button", body.includes("open") && body.includes("Close"));
  check("the broker's control of the relays is off by default and explained", body.includes("Let the broker move the relays") && body.includes("armor/device/<node>/<relay>") && !body.includes("Fall back to open after"));
  await ev(`(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('The broker moves the relays')); const box = [...c.querySelectorAll('label')].find(l => l.textContent.includes('Let the broker move the relays')).querySelector('input'); box.checked = true; box.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await sleep(400);
  body = await text();
  check("turning it on shows the fall back time and how to add the devices in Studio", body.includes("Fall back to open after") && body.includes("electrical-a1b2c3/<relay name>") && body.includes("electrical-relays electrical-a1b2c3 on"), body.slice(0, 200));
  await clickButton("Discard changes"); await sleep(400);
  await clickButton("Close"); await sleep(900);
  check("closing a relay shows it closed", (await ev("[...document.querySelectorAll('.pill')].some(p => p.textContent.trim() === 'closed')")) === true);
  const io = await (await fetch("http://127.0.0.1:18122/api/v1/io", { headers: { Cookie: cookie } })).json();
  check("the node reports the relay closed", io.relays[0].on === true && io.relays[1].on === false, JSON.stringify(io.relays[0]));
  await shot(`epanel-${board}-io.png`);
  // an analog input on a pin that is not ADC1 is refused
  await ev(`(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('Input 3')); const box = c.querySelector('input[type=checkbox]'); box.checked = true; box.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await sleep(400);
  const pinOptions = await ev(`(() => { const c = [...document.querySelectorAll('section.card')].find(x => x.querySelector('h2')?.textContent.startsWith('Input 3')); const sel = [...c.querySelectorAll('select')][0]; return [...sel.options].map(o => o.value).filter(v => v !== '-1').map(Number); })()`);
  check("an analog input is offered only ADC1 pins", pinOptions.length > 0 && pinOptions.every(g => g >= 1 && g <= 10), JSON.stringify(pinOptions));
  await clickButton("Discard changes"); await sleep(400);

  // ---- the network page has the Bluetooth setting
  await goto("network");
  body = await text();
  check("the network page has the Bluetooth card", body.includes("Bluetooth") && body.includes("ARMOR app on a phone"), body.slice(0, 100));
  await ev(`(() => { const sel = [...document.querySelectorAll('select')].find(s => [...s.options].some(o => o.value === 'always')); sel.value = 'always'; sel.dispatchEvent(new Event('change', { bubbles: true })); })()`);
  await clickButton("Save"); await sleep(800);
  const saved3 = await (await fetch("http://127.0.0.1:18122/api/v1/config", { headers: { Cookie: cookie } })).json();
  check("the Bluetooth mode is saved", saved3.config.ble && saved3.config.ble.mode === "always", JSON.stringify(saved3.config.ble));
  await shot(`epanel-${board}-network.png`);

  // ---- the firmware page can ask GitHub for a newer version (every node has it)
  await goto("update");
  body = await text();
  check("the firmware page offers the GitHub check", body.includes("Check GitHub for a new version") && body.includes("Check now"), body.slice(0, 200));
  await clickButton("Check now"); await sleep(700);
  body = await text();
  check("the check says a newer version is available and offers to install it", body.includes("Version 9.9.9 is available.") && body.includes("Download and install"), body.slice(0, 300));
  await clickButton("Download and install"); await sleep(1200);
  check("the install shows its progress", /Downloading the firmware from GitHub/.test(await text()));
  await ev("location.reload()"); await sleep(1500);

  // ---- every page in every language: no raw keys, no missing texts, no console errors
  const pages = ["overview", "meters", "readings", "zigbee", "io", "network", "wifi", "broker", "users", "update"];
  const langs = await ev("LANGS.map(l => l[0])");
  let rawBad = 0;
  for (let li = 0; li < langs.length; li += 1) {
    await ev(`(() => { lang = ${li}; document.documentElement.lang = LANGS[${li}][0]; })()`);
    for (const p of pages) {
      await ev(`location.hash = '#/${p}'`); await sleep(300);
      const raw = await ev(`(() => { const keys = new Set(Object.keys(L)); const bad = []; document.querySelectorAll('h1,h2,h3,label,span,button,p,dt,th,option,small,a').forEach(n => { const s = n.childNodes.length === 1 && n.childNodes[0].nodeType === 3 ? n.textContent.trim() : ''; if (s && keys.has(s) && L[s][0] !== s) bad.push(s); if (/^\\?\\?|undefined|\\[object/.test(s)) bad.push(s); }); return bad; })()`);
      if (raw.length) { rawBad += 1; check(`no raw text keys on ${p} in ${langs[li]}`, false, raw.join(",")); }
      if (li === 5 && (p === "meters" || p === "readings")) await shot(`epanel-${board}-${p}-${langs[li]}.png`);
    }
  }
  check(`all ${pages.length} pages in all ${langs.length} languages have their texts`, rawBad === 0);

  // ---- phone width
  await send("Emulation.setDeviceMetricsOverride", { width: 390, height: 800, deviceScaleFactor: 2, mobile: true });
  await ev(`(() => { lang = 0; localStorage.setItem('armor_lang', 'en'); })()`);
  for (const p of ["meters", "readings", "zigbee", "io"]) {
    await ev(`location.hash = '#/${p}'`); await sleep(500);
    const overflow = await ev("document.documentElement.scrollWidth - document.documentElement.clientWidth");
    check(`the ${p} page fits a phone's width`, overflow <= 1, `overflow ${overflow}px`);
    await shot(`epanel-${board}-${p}-phone.png`);
  }

  // ---- a fresh node shows the set-up screen
  await send("Emulation.setDeviceMetricsOverride", { width: 1300, height: 900, deviceScaleFactor: 1, mobile: false });
  await send("Network.clearBrowserCookies");
  await send("Page.navigate", { url: "http://127.0.0.1:18123/" }); await sleep(1500);
  check("a fresh node shows the set-up screen", (await text()).includes("Set up this node"));
  await shot(`epanel-${board}-setup.png`);

  const real = errors.filter(e => !/favicon|401|403|422/.test(e));
  check("no errors in the browser console", real.length === 0, JSON.stringify(real).slice(0, 400));
  console.log(`${results.filter(Boolean).length}/${results.length} checks passed`);
} catch (e) { console.log("FAILED", e); } finally { cleanup(); process.exit(0); }
