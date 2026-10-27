#!/usr/bin/env node
// 使用 PassportSim 发布版 WASM 执行真实固件；不读取设备备份或登录态。
const fs = require('node:fs');
const crypto = require('node:crypto');
const path = require('node:path');
async function main() {
  const [wasmPath, imagePath, elfPath, reportPath, scope = 'baseline'] = process.argv.slice(2);
  if (!reportPath) throw new Error('Usage: node run_passportsim.cjs <wasm> <merged.bin> <app.elf> <report.json> [baseline|diagnostic-wifi|diagnostic-wifi-wrong-password|diagnostic-wifi-absent]');
  if (!['baseline', 'diagnostic-wifi', 'diagnostic-wifi-wrong-password', 'diagnostic-wifi-absent'].includes(scope)) throw new Error('Unknown scope');
  const diagnostic = scope.startsWith('diagnostic-wifi');
  const wrongPassword = scope === 'diagnostic-wifi-wrong-password';
  const absent = scope === 'diagnostic-wifi-absent';
  const report = { emulator: 'PassportSim WASM', hardware: false, scope, checks: [], passed: false, releaseAcceptance: false };
  report.wasm = { sha256: crypto.createHash('sha256').update(fs.readFileSync(wasmPath)).digest('hex') };
  if (diagnostic) report.caveat = 'Separate IDF 5.5.3 build, unsupported esp_wifi_set_ps omitted only in temporary source copy; not release acceptance';
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
  const e = instance.exports, decoder = new TextDecoder();
  let handle = 0;
  const copy = bytes => { const p = e.pemu_alloc(bytes.length); if (!p) throw new Error('WASM allocation failed'); new Uint8Array(e.memory.buffer).set(bytes, p); return p; };
  const result = p => {
    if (!p) throw new Error('WASM returned no result');
    try {
      const v = new DataView(e.memory.buffer, p, 12);
      const bytes = new Uint8Array(e.memory.buffer).slice(v.getUint32(0, true), v.getUint32(0, true) + v.getUint32(4, true));
      if (v.getUint32(8, true)) throw new Error(decoder.decode(bytes));
      return bytes;
    } finally { e.pemu_result_free(p); }
  };
  const call = (cmd, args) => {
    const bytes = Buffer.from(JSON.stringify({ cmd, args })), p = copy(bytes);
    try {
      const output = JSON.parse(decoder.decode(result(e.pemu_call(handle, p, bytes.length))));
      // 任何阶段的失败都终止验收，避免最终页面正常掩盖中途故障。
      if (output.json?.result === 'fail') throw new Error(cmd + ': assertion failed');
      if (output.receipt?.hle?.tripwires_hit?.length) throw new Error(cmd + ': emulator tripwire');
      if (/Guru Meditation|Task watchdog got triggered|assert failed:/.test(output.text || ''))
        throw new Error(cmd + ': guest panic or watchdog');
      return output;
    }
    finally { e.pemu_free(p, bytes.length); }
  };
  try {
    report.abi = e.pemu_abi_version();
    if (report.abi !== 3) throw new Error('Unsupported WASM ABI: ' + report.abi);
    const cfg = Buffer.from(JSON.stringify({ seed: 1, profile: 'device' })), p = copy(cfg);
    let builder;
    try { builder = e.pemu_new(p, cfg.length); } finally { e.pemu_free(p, cfg.length); }
    for (const [kind, file] of [[1, imagePath], [2, elfPath]]) {
      const bytes = fs.readFileSync(file), ptr = copy(bytes);
      report[path.extname(file).slice(1)] = { sha256: crypto.createHash('sha256').update(bytes).digest('hex'), bytes: bytes.length };
      try { result(e.pemu_load(builder, kind, ptr, bytes.length)); }
      finally { e.pemu_free(ptr, bytes.length); }
    }
    const built = result(e.pemu_build(builder));
    handle = new DataView(built.buffer, built.byteOffset).getUint32(0, true);
    report.checks.push({ name: 'load firmware and ELF', passed: true });
    report.boot = call('run', { until: 'serial:/Calling app_main/', timeout: '15s', wall_budget_ms: 30000 });
    report.checks.push({ name: 'boot to app_main', passed: report.boot.json?.result === 'pass' });
    report.fidelity = call('inspect', { what: ['fidelity'] });
    report.advance = call('run', { for: '10s', wall_budget_ms: 30000, fail_if: ['serial:/Guru Meditation/', 'serial:/Task watchdog got triggered/', 'serial:/assert failed:/'] });
    report.checks.push({ name: '10 second boot without panic or watchdog', passed: report.advance.json?.result === 'pass' });
    report.uiBefore = call('ui', { include_style: true });
    report.checks.push({ name: 'welcome screen', passed: report.uiBefore.text.includes('Folo Music') });
    if (diagnostic) report.ap = call('wifi_ap', { ssid: 'Folo-Test-Lab', auth: 'wpa2-psk', key: wrongPassword ? 'different-test-key' : 'qwertyui', channel: 6, rssi: -40 });
    report.buttonOK = call('input', { button: 'ok', action: 'click', then_run: '2s' });
    report.scan = call('run', { for: '10s', wall_budget_ms: 30000, fail_if: ['serial:/Guru Meditation/', 'serial:/Task watchdog got triggered/'] });
    if (report.scan.json?.result !== 'pass') throw new Error('Wi-Fi scan did not complete successfully');
    report.uiAfter = call('ui', { include_style: true });
    if (diagnostic) {
      report.checks.push({ name: 'scan lists virtual access point', passed: report.uiAfter.text.includes('Folo-Test-Lab') });
      report.connect = call('input', { button: 'ok', action: 'click', then_run: '5s' });
      report.uiKeyboard = call('ui', { include_style: true });
      report.checks.push({ name: 'password keyboard opens', passed: /密码/.test(report.uiKeyboard.text) });
      report.keyboardInput = [];
      for (let i = 0; i < 8; ++i) {
        if (i) call('input', { button: 'down', action: 'click', then_run: '400ms' });
        report.keyboardInput.push(call('input', { button: 'ok', action: 'click', then_run: '400ms' }));
      }
      report.uiPassword = call('ui', { include_style: true });
      report.checks.push({ name: 'eight keyboard characters accepted', passed: report.uiPassword.text.includes('8 / 64') && report.uiPassword.text.includes('********') });
      report.actions = call('input', { button: 'ok', action: 'hold', duration: '1700ms', then_run: '400ms' });
      report.uiActions = call('ui', { include_style: true });
      report.checks.push({ name: 'connection menu opens', passed: /继续输入/.test(report.uiActions.text) });
      if (absent) report.removeAP = call('wifi_ap', { ssid: 'Folo-Test-Lab', remove: true });
      report.submit = call('input', { button: 'ok', action: 'click', then_run: wrongPassword || absent ? '30s' : '8s' });
      report.uiConnected = call('ui', { include_style: true });
      if (wrongPassword || absent) {
        report.checks.push({ name: absent ? 'absent AP shows connection failure' : 'wrong password shows connection failure', passed: report.uiConnected.text.includes('网络连接失败') && !report.uiConnected.text.includes('网络已连接') });
        if (absent) report.restoreAP = call('wifi_ap', { ssid: 'Folo-Test-Lab', auth: 'wpa2-psk', key: 'qwertyui', channel: 6, rssi: -40 });
        report.changeNetwork = call('input', { button: 'down', action: 'click', then_run: '10s' });
        report.uiRetryScan = call('ui', { include_style: true });
        report.checks.push({ name: 'failure allows another network scan', passed: report.uiRetryScan.text.includes('Folo-Test-Lab') && !report.uiRetryScan.text.includes('网络连接失败') });
      } else report.checks.push({ name: 'keyboard password connects', passed: report.uiConnected.text.includes('网络已连接') });
    }
    report.serial = call('serial', { op: 'read', max_bytes: 24000 });
    report.network = call('net_http', { op: 'status' });
    report.unverified = ['Production esp_wifi_set_ps', 'NetEase TLS and QR session', 'Playback with uncached page requests', 'Network recovery and audio underruns'];
    // 通过仅指所选诊断范围，始终单独记录发布验收未完成。
    report.passed = diagnostic && report.checks.every(check => check.passed);
    if (scope === 'baseline') report.error = 'Release acceptance is incomplete: NetEase TLS, concurrent playback and recovery scenarios are not implemented';
  } catch (error) { report.error = error.message; }
  finally {
    if (handle) {
      try { report.serialFinal = call('serial', { op: 'read', max_bytes: 24000 }); } catch (error) { report.serialError = error.message; }
      try { report.fidelityFinal = call('inspect', { what: ['fidelity', 'heap'] }); } catch (error) { report.fidelityError = error.message; }
      const trips = report.serialFinal?.receipt?.hle?.tripwires_hit;
      if (trips?.length) report.passed = false;
      e.pemu_drop(handle);
    }
    fs.writeFileSync(reportPath, JSON.stringify(report, null, 2) + '\n');
  }
  console.log(JSON.stringify({ passed: report.passed, scope, releaseAcceptance: false, checks: report.checks, error: report.error, report: reportPath }));
  process.exitCode = report.passed ? 0 : 1;
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
