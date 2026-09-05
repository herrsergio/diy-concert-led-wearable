#pragma once

#include <Arduino.h>

// The control UI, served straight from flash with server.send_P().
//
// Held in PROGMEM rather than on SPIFFS on purpose: a filesystem image needs a
// separate `pio run --target uploadfs` step, and forgetting it after an
// otherwise successful flash serves a 404 or a stale page. Keeping the page in
// the binary means one upload command and no way for the two to disagree.
//
// Isolated in its own header so web_server.cpp stays readable.

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SKZ LED Strip</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 20px 16px 40px;
    font: 16px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: #14141a; color: #f0f0f5;
    max-width: 520px; margin-inline: auto;
  }
  h1 { font-size: 20px; margin: 0 0 4px; letter-spacing: .02em; }
  .sub { color: #8a8a9a; font-size: 13px; margin: 0 0 24px; }
  h2 { font-size: 12px; text-transform: uppercase; letter-spacing: .09em;
       color: #8a8a9a; margin: 26px 0 10px; font-weight: 600; }
  .row { display: flex; gap: 8px; flex-wrap: wrap; }
  button {
    flex: 1 1 auto; min-width: 96px; min-height: 52px;
    padding: 12px 14px; font-size: 15px; font-weight: 500;
    color: #f0f0f5; background: #22222c;
    border: 1px solid #33333f; border-radius: 10px;
    cursor: pointer; transition: background .12s, border-color .12s;
  }
  button:active { background: #2c2c38; }
  button[aria-pressed="true"] {
    background: #d4315f; border-color: #d4315f; color: #fff;
  }
  .slider-head { display: flex; justify-content: space-between; align-items: baseline; }
  .val { font-variant-numeric: tabular-nums; color: #f0f0f5; font-size: 15px; }
  input[type=range] {
    width: 100%; margin: 12px 0 0; height: 40px; accent-color: #d4315f;
  }
  .note { color: #8a8a9a; font-size: 12px; margin: 8px 0 0; }
  #status { margin-top: 26px; font-size: 12px; color: #6a6a7a; min-height: 1.5em; }
  #status.err { color: #ff7a92; }
</style>
</head>
<body>
<h1>SKZ LED Strip</h1>
<p class="sub">Concert wearable control</p>

<h2>Mode</h2>
<div class="row" id="modes">
  <button data-mode="0" aria-pressed="false">Audio Reactive</button>
  <button data-mode="2" aria-pressed="false">Manual</button>
</div>
<p class="note">Manual holds the last frame on the strip; patterns stop updating.</p>

<h2>Pattern</h2>
<div class="row" id="patterns"></div>

<h2>Brightness</h2>
<div class="slider-head">
  <span>Level</span><span class="val" id="bval">--</span>
</div>
<input type="range" id="bright" min="0" max="255" value="128" step="1">

<div id="status">Connecting...</div>

<script>
var state = null;
var sending = false;
var lastSent = 0;
var pendingBright = null;

function say(msg, isErr) {
  var el = document.getElementById('status');
  el.textContent = msg;
  el.className = isErr ? 'err' : '';
}

function render() {
  // patterns[] is missing only if the firmware had to truncate the JSON, which
  // should not happen. Bail out rather than throw, so the page stays usable.
  if (!state || !state.patterns) return;
  var mb = document.querySelectorAll('#modes button');
  for (var i = 0; i < mb.length; i++) {
    var on = (+mb[i].dataset.mode === state.mode);
    mb[i].setAttribute('aria-pressed', on ? 'true' : 'false');
  }
  var box = document.getElementById('patterns');
  if (box.childElementCount !== state.patterns.length) {
    box.innerHTML = '';
    state.patterns.forEach(function (name, i) {
      var b = document.createElement('button');
      b.textContent = name;
      b.dataset.pattern = i;
      b.setAttribute('aria-pressed', 'false');
      b.addEventListener('click', function () { send({ pattern: i }); });
      box.appendChild(b);
    });
  }
  var pb = box.children;
  for (var j = 0; j < pb.length; j++) {
    pb[j].setAttribute('aria-pressed', j === state.pattern ? 'true' : 'false');
  }
  var sl = document.getElementById('bright');
  if (document.activeElement !== sl) sl.value = state.brightness;
  document.getElementById('bval').textContent = state.brightness;
}

function load() {
  fetch('/api/state').then(function (r) {
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }).then(function (j) {
    state = j; render(); say('Connected');
  }).catch(function (e) { say('Cannot reach strip: ' + e.message, true); });
}

function send(changes) {
  var q = Object.keys(changes).map(function (k) {
    return k + '=' + encodeURIComponent(changes[k]);
  }).join('&');
  sending = true;
  fetch('/api/set?' + q, { method: 'POST' }).then(function (r) {
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }).then(function (j) {
    state = j; render(); say('Updated');
  }).catch(function (e) {
    say('Update failed: ' + e.message, true);
  }).then(function () {
    sending = false;
    if (pendingBright !== null) {
      var v = pendingBright; pendingBright = null; send({ brightness: v });
    }
  });
}

document.querySelectorAll('#modes button').forEach(function (b) {
  b.addEventListener('click', function () { send({ mode: +b.dataset.mode }); });
});

var slider = document.getElementById('bright');
slider.addEventListener('input', function () {
  document.getElementById('bval').textContent = slider.value;
  var now = Date.now();
  // Throttle while dragging so one gesture does not queue dozens of requests.
  if (sending || now - lastSent < 120) { pendingBright = +slider.value; return; }
  lastSent = now;
  send({ brightness: +slider.value });
});
slider.addEventListener('change', function () {
  // Always deliver the released value, even if the last move was throttled out.
  send({ brightness: +slider.value });
});

load();
</script>
</body>
</html>
)HTML";
