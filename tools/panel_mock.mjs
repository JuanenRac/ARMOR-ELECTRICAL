#!/usr/bin/env node
/**
 * ARMOR-ELECTRICAL - a stand-in for an electrical node, to work on the web panel without a board.
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 *   node tools/panel_mock.mjs [port]          (default 8090)     open http://127.0.0.1:8090/
 *
 * It serves the panel from panel/ (text.js is joined in front of app.js, as tools/pack_panel.py does) and answers /api/v1 like the firmware
 * does, from memory: a fresh mock is in set-up (code TESTCODE); `--user admin:adminpass123` starts it with an administrator. It is a development
 * tool: it checks far less than the firmware, and its numbers are made up. The meters answer as the firmware's status does: a good one, a silent one, a garbled one and one that reads a DC bus.
 */
import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const panel = path.join(here, "..", "panel");
const port = Number(process.argv.find(a => /^\d+$/.test(a)) ?? 8090);
// --board s3-eth: the mock plays the Waveshare ESP32-S3-ETH (a cable, the W5500 pins reserved); the default is the s3-wifi board
const board = process.argv.includes("--board") ? process.argv[process.argv.indexOf("--board") + 1] : "s3-wifi";
const wired = board === "s3-eth";
const seeded = process.argv.includes("--user") ? process.argv[process.argv.indexOf("--user") + 1] : "";

const users = new Map();
if (seeded) { const [name, password] = seeded.split(":"); users.set(name, { password, role: "admin" }); }
const sessions = new Map();
const started = Date.now();
let rebootAt = 0;
let logText = "I (1200) armor-node: A.R.M.O.R. node armor-a1b2c3, firmware 0.2.3\nI (1500) armor-net: link up\nI (2600) armor-net: address 192.168.0.181, gateway 192.168.0.1, netmask 255.255.255.0 (wire)\nW (9000) armor-radar: radar 2: no data (RX not connected, no power or the wrong pin) (0 bytes, 0 frames, 0 bad)\n";

const config = {
  v: 1, node: { id: "electrical-a1b2c3", name: "House panel", hostname: "" },
  uplink: wired ? "ethernet" : "wifi", ip: { dhcp: true, address: "", netmask: "255.255.255.0", gateway: "", dns1: "", dns2: "" },
  ap: { enabled: true, ssid: "ARMOR-ELECTRICAL-A1B2C3", security: "wpa2", password_set: true, channel: 0, hidden: false, max_clients: 8, tx_power_dbm: 15, bandwidth_mhz: 20, country: "ES" },
  sta: { enabled: true, ssid: "HomeRouter", password_set: true },
  mqtt: { enabled: true, uri: "mqtt://192.168.0.180:18883", username: "electrical-node-electrical-a1b2c3", password_set: true, heartbeat_s: 10, ntp: "pool.ntp.org" },
  bus: { rx: 16, tx: 15, baud: 9600, poll_s: 2 },
  meters: Array.from({ length: 16 }, (_, i) => ({
    enabled: i < 4, channel: ["grid", "heater", "dc-bus", "oven"][i] ?? `meter${i + 1}`, label: ["Grid input", "Water heater", "Battery bus", ""][i] ?? "", model: i === 2 ? "dc" : "ac", address: i + 1,
  })),
  web: { mode: "both" },
  ble: { mode: "setup" },
  ui: { language: "en" },
};

const catalog = [];
for (let gpio = 0; gpio <= 48; ++gpio) {
  if (gpio >= 22 && gpio <= 25) continue;
  let use = "free", reason = "", note = "", header = true;
  if (gpio >= 26 && gpio <= 32) { use = "reserved"; reason = "flash"; header = false; }
  else if (wired && gpio >= 9 && gpio <= 14) { use = "reserved"; reason = "ethernet"; header = false; }
  else if (wired && gpio === 8) { use = "reserved"; reason = "camera"; header = false; }
  else if (wired && gpio >= 4 && gpio <= 7) { use = "caution"; note = "sdcard"; }
  else if (gpio >= 33 && gpio <= 37) { use = "reserved"; reason = "psram"; header = gpio >= 35; }
  else if (gpio === 19 || gpio === 20) { use = "reserved"; reason = "usb"; }
  else if (gpio === 0) { use = "caution"; note = "boot"; }
  else if ([3, 45, 46].includes(gpio)) { use = "caution"; note = "strapping"; }
  else if (gpio === 43 || gpio === 44) { use = "caution"; note = "usb_serial"; }
  else if (gpio === 48) { use = "caution"; note = "led"; }
  catalog.push({ gpio, use, reason, note, on_header: header });
}
const NUMBERS = [
  { voltage_v: 231.4, current_a: 12.41, power_w: 2871.3, energy_kwh: 1834.212 },
  { voltage_v: 231.2, current_a: 6.02, power_w: 1391.5, energy_kwh: 402.881 },
  { voltage_v: 52.31, current_a: 18.4, power_w: 962.5, energy_kwh: 96.4 },
  null,
];
// meter 1 reads, 2 reads, 3 reads a DC bus, 4 is silent; any further one enabled by hand is garbled
const stateOf = i => (!config.meters[i].enabled ? "disabled" : i < 3 ? "reading" : i === 3 ? "silent" : "garbled");
const meters = () => config.meters.map((m, i) => {
  const state = stateOf(i), on = state === "reading";
  const tick = Math.floor((Date.now() - started) / 2000);
  return { meter: i + 1, enabled: m.enabled, channel: m.channel, label: m.label, model: m.model, address: m.address, state, error: state === "silent" ? "silent" : state === "garbled" ? "crc" : "",
    replies_ok: on ? 200 + tick : 0, replies_bad: state === "garbled" ? 5 + tick : 0, timeouts: state === "silent" ? 40 + tick : 0,
    ...(on ? { reading: { ...NUMBERS[i], power_w: Number((NUMBERS[i].power_w + (tick % 7) * 3.1).toFixed(1)), alarm: false } } : {}) };
});
const busStatus = () => {
  const tick = Math.floor((Date.now() - started) / 2000);
  const active = config.meters.filter(m => m.enabled).length;
  return { state: active ? "running" : "disabled", error: "", rx: config.bus.rx, tx: config.bus.tx, baud: config.bus.baud, poll_s: config.bus.poll_s, bytes_rx: active * 25 * tick, bytes_tx: active * 8 * tick, messages: tick };
};
const readings = () => {
  const channels = meters().filter(m => m.state === "reading").map(m => ({ id: m.channel, domain: m.model, ...(m.label ? { label: m.label } : {}), ...m.reading,
    ...(m.model === "ac" ? { frequency_hz: 50, power_factor: 0.98 } : {}) }));
  return channels.length ? [{ kind: "electrical", node_id: config.node.id, timestamp_ms: Date.now(), switching_enabled: false, channels }] : [];
};

const json = (response, code, body, headers = {}) => { response.writeHead(code, { "Content-Type": "application/json", "Cache-Control": "no-store", ...headers }); response.end(JSON.stringify(body)); };
const bodyOf = request => new Promise(resolve => { const chunks = []; request.on("data", c => chunks.push(c)); request.on("end", () => resolve(Buffer.concat(chunks))); });
const tokenOf = request => /armor_session=([0-9a-f]+)/.exec(request.headers.cookie ?? "")?.[1] ?? "";

function status() {
  return {
    node_id: config.node.id, name: config.node.name, version: "0.0.3", uptime_s: Math.floor((Date.now() - started) / 1000) + 5400, reset_reason: "power_on", heap_free: 182000, heap_min: 151000, psram_free: 7400000, partition: "ota_0",
    network: { board, ethernet_available: wired, ethernet_ok: true, layout: wired ? "ethernet+ap" : "wifi-station+ap", link_up: true, has_ip: true, ip: "192.168.0.181", netmask: "255.255.255.0", gateway: "192.168.0.1", dns: "192.168.0.1", mac: "34:85:18:a1:b2:c3",
      ap_active: config.ap.enabled, ap_setup: users.size === 0, ap_ssid: users.size === 0 ? "ARMOR-SETUP-A1B2C3" : config.ap.ssid, ap_channel: 6, ap_clients: 1, sta_connected: true, sta_ssid: config.sta.ssid, sta_rssi: -52 },
    mqtt: { enabled: config.mqtt.enabled, connected: true, clock_set: true, published: 400 + Math.floor((Date.now() - started) / 5000), dropped: 0 },
    web: { mode: config.web.mode, https: config.web.mode !== "http", cert_sha256: "a3f1c07d9e2b4c58a7106f3de9b2c4815d6e7f80a1b2c3d4e5f60718293a4b5c" },
    bus: busStatus(),
    meters: meters(),
  };
}

const problems = doc => {
  const list = [];
  if (doc.ap?.enabled && !String(doc.ap.ssid ?? "").trim()) list.push({ path: "ap.ssid", code: "required" });
  if (doc.ap?.enabled && doc.ap.security !== "open" && doc.ap.password !== undefined && doc.ap.password.length > 0 && doc.ap.password.length < 8) list.push({ path: "ap.password", code: "invalid_key" });
  if (doc.mqtt?.enabled && !/^mqtts?:\/\/.+/.test(doc.mqtt.uri ?? "")) list.push({ path: "mqtt.uri", code: "invalid" });
  if (doc.bus && (![1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200].includes(doc.bus.baud ?? 9600))) list.push({ path: "bus.baud", code: "range" });
  const seen = new Map();
  (doc.meters ?? []).forEach((m, i) => {
    if (!m.enabled) return;
    const base = `meters.${i}.`;
    if (!/^[a-z0-9][a-z0-9_-]{0,31}$/.test(m.channel ?? "")) list.push({ path: base + "channel", code: m.channel ? "invalid" : "required" });
    else if (seen.has("c" + m.channel)) list.push({ path: base + "channel", code: "conflict" }); else seen.set("c" + m.channel, i);
    if (!(m.address >= 1 && m.address <= 247)) list.push({ path: base + "address", code: "range" });
    else if (seen.has("a" + m.address)) list.push({ path: base + "address", code: "conflict" }); else seen.set("a" + m.address, i);
  });
  return list;
};

const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://x");
  if (request.method === "GET" && ["/", "/index.html"].includes(url.pathname)) { response.writeHead(200, { "Content-Type": "text/html" }); return response.end(readFileSync(path.join(panel, "index.html"))); }
  if (request.method === "GET" && url.pathname === "/style.css") { response.writeHead(200, { "Content-Type": "text/css" }); return response.end(readFileSync(path.join(panel, "style.css"))); }
  if (request.method === "GET" && url.pathname === "/app.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); return response.end(readFileSync(path.join(panel, "text.js"), "utf8") + "\n" + readFileSync(path.join(panel, "app.js"), "utf8")); }
  if (!url.pathname.startsWith("/api/v1/")) return json(response, 404, { error: "not_found" });

  const route = url.pathname.slice(8), method = request.method;
  const raw = await bodyOf(request);
  let body = {};
  if (raw.length && !url.pathname.endsWith("/ota")) { try { body = JSON.parse(raw.toString("utf8")); } catch { return json(response, 400, { error: "not_json" }); } }
  const session = sessions.get(tokenOf(request));
  const setup = users.size === 0;

  if (method === "GET" && route === "session") return json(response, 200, { setup, authenticated: !!session, user: session?.user ?? "", role: session?.role ?? "", node_id: config.node.id, language: config.ui.language, version: "0.2.3", setup_ssid: setup ? "ARMOR-SETUP-A1B2C3" : "", mac: "34:85:18:a1:b2:c3", board, ethernet: wired });
  if (method === "POST" && route === "setup") {
    if (!setup) return json(response, 403, { error: "forbidden" });
    if (body.code !== "TESTCODE") return json(response, 403, { error: "wrong_code" });
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.user ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    users.set(body.user, { password: body.password, role: "admin" });
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "POST" && route === "login") {
    const user = users.get(body.user);
    if (!user || user.password !== body.password) return json(response, 401, { error: "wrong_credentials" });
    const token = [...Array(48)].map(() => "0123456789abcdef"[Math.floor(Math.random() * 16)]).join("");
    sessions.set(token, { user: body.user, role: user.role });
    return json(response, 200, { ok: true, restart_required: false }, { "Set-Cookie": `armor_session=${token}; Path=/; HttpOnly; SameSite=Strict` });
  }
  if (method === "POST" && route === "logout") { sessions.delete(tokenOf(request)); return json(response, 200, { ok: true }); }
  if (setup) return json(response, 403, { error: "setup_required" });
  if (!session) return json(response, 401, { error: "unauthorized" });
  if (method !== "GET" && request.headers["x-requested-with"] !== "armor") return json(response, 403, { error: "forbidden" });
  const admin = session.role === "admin";
  const needAdmin = () => { if (!admin) { json(response, 403, { error: "forbidden" }); return false; } return true; };

  if (method === "GET" && route === "status") return json(response, 200, status());
  if (method === "GET" && route === "wifi/scan") { if (!needAdmin()) return; return json(response, 200, { networks: [{ ssid: "HomeRouter", rssi: -48, channel: 6, security: "wpa2" }, { ssid: "Neighbour", rssi: -71, channel: 11, security: "wpa2wpa3" }, { ssid: "CafeOpen", rssi: -80, channel: 1, security: "open" }] }); }
  if (method === "GET" && route === "config") return json(response, 200, { config, channel_auto: 6, firmware: "0.2.3" });
  if (method === "PUT" && route === "config") {
    if (!needAdmin()) return;
    const list = problems(body);
    if (list.length) return json(response, 422, { error: "invalid", problems: list });
    for (const key of Object.keys(body)) {
      const value = body[key];
      if (key === "meters" && Array.isArray(value)) value.forEach((m, i) => Object.assign(config.meters[i], m));
      else if (value && typeof value === "object" && !Array.isArray(value) && config[key] && typeof config[key] === "object") Object.assign(config[key], value);
      else config[key] = value;
    }
    for (const section of [config.ap, config.sta, config.mqtt]) if (typeof section.password === "string" && section.password) { section.password_set = true; delete section.password; } else delete section.password;
    return json(response, 200, { ok: true, restart_required: true });
  }
  if (method === "GET" && route === "meters") return json(response, 200, { bus: busStatus(), meters: meters(), catalog });
  if (method === "GET" && route === "readings") return json(response, 200, readings());
  if (method === "GET" && route === "users") { if (!needAdmin()) return; return json(response, 200, [...users].map(([name, u]) => ({ name, role: u.role }))); }
  if (method === "POST" && route === "users") {
    if (!needAdmin()) return;
    if (!/^[a-z0-9_.-]{3,32}$/.test(body.name ?? "")) return json(response, 422, { error: "invalid_name" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    if (users.has(body.name)) return json(response, 409, { error: "exists" });
    users.set(body.name, { password: body.password, role: body.role === "admin" ? "admin" : "viewer" });
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const user = users.get(route.slice(6));
    if (!user) return json(response, 404, { error: "not_found" });
    if (body.role) user.role = body.role;
    if (body.password) user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "DELETE" && route.startsWith("users/")) {
    if (!needAdmin()) return;
    const name = route.slice(6);
    if (users.get(name)?.role === "admin" && [...users.values()].filter(u => u.role === "admin").length === 1) return json(response, 409, { error: "last_admin" });
    users.delete(name);
    return json(response, users.has(name) ? 500 : 200, { ok: true, restart_required: false });
  }
  if (method === "PUT" && route === "account") {
    const user = users.get(session.user);
    if (user.password !== body.current) return json(response, 403, { error: "wrong_password" });
    if ((body.password ?? "").length < 8) return json(response, 422, { error: "weak_password" });
    user.password = body.password;
    return json(response, 200, { ok: true, restart_required: false });
  }
  if (method === "GET" && route === "log") return json(response, 200, { next: logText.length, text: logText.slice(Number(url.searchParams.get("from") ?? 0)) });
  if (method === "POST" && route === "reboot") { if (!needAdmin()) return; rebootAt = Date.now(); return json(response, 200, { ok: true, restart_required: false }); }
  if (method === "POST" && route === "factory-reset") { if (!needAdmin()) return; if (body.confirm !== "RESET") return json(response, 422, { error: "confirm_required" }); users.clear(); return json(response, 200, { ok: true, restart_required: true }); }
  if (method === "POST" && route === "ota") { if (!needAdmin()) return; return json(response, 200, { ok: true, restart_required: true, version: "0.2.4", bytes: raw.length, sha256: "0".repeat(64) }); }
  return json(response, 404, { error: "not_found" });
});
server.listen(port, "127.0.0.1", () => console.log(`ARMOR node panel mock on http://127.0.0.1:${port}/ (${users.size ? "signed-in users: " + [...users.keys()].join(", ") : "set-up code TESTCODE"})`));
