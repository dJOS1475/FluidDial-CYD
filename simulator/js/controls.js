/*
 * controls.js — the side "bench" panel that drives the simulated machine and
 * comms state (the things a real FluidNC controller + the pendant hardware
 * would supply).  Changing any control updates the shared state globals and
 * repaints the active screen, so you can see how the UI reacts to live data.
 */

const _ctl = {}; // id -> element

function _el(tag, props = {}, children = []) {
  const e = document.createElement(tag);
  for (const k in props) {
    if (k === "class") e.className = props[k];
    else if (k === "html") e.innerHTML = props[k];
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), props[k]);
    else e.setAttribute(k, props[k]);
  }
  for (const c of [].concat(children)) e.appendChild(typeof c === "string" ? document.createTextNode(c) : c);
  return e;
}

function _row(label, input) {
  return _el("label", { class: "ctl-row" }, [_el("span", { class: "ctl-label" }, label), input]);
}

function _select(id, options, onchange) {
  const s = _el("select", { id, onchange });
  for (const o of options) {
    const opt = _el("option", { value: o.v }, o.t);
    s.appendChild(opt);
  }
  _ctl[id] = s;
  return s;
}

function _num(id, step, onchange) {
  const i = _el("input", { id, type: "number", step: String(step), oninput: onchange });
  _ctl[id] = i;
  return i;
}

function _range(id, min, max, step, oninput) {
  const i = _el("input", { id, type: "range", min: String(min), max: String(max), step: String(step), oninput });
  _ctl[id] = i;
  return i;
}

function _check(id, onchange) {
  const i = _el("input", { id, type: "checkbox", onchange });
  _ctl[id] = i;
  return i;
}

function applyLink() {
  const v = _ctl["link"].value;
  if (v === "off") {
    pendantConnected = false; pendantSynced = false;
    pendantMachine.connectionStatus = "N/C";
    pendantMachine.status = "N/C";
  } else if (v === "connecting") {
    pendantConnected = false; pendantSynced = false;
    pendantMachine.connectionStatus = "Connecting";
  } else {
    pendantConnected = true; pendantSynced = true;
    pendantMachine.connectionStatus = "Connected";
    if (pendantMachine.status === "N/C") pendantMachine.status = _ctl["status"].value;
  }
  syncControlsFromState();
  drawCurrentPendantScreen();
}

function repaint() { drawCurrentPendantScreen(); }

function buildControls() {
  const root = document.getElementById("controls");
  root.innerHTML = "";

  // -- Navigation --
  root.appendChild(_el("h3", {}, "View"));
  const screenOpts = Object.keys(SCREEN_LABELS).map((k) => ({ v: k, t: SCREEN_LABELS[k] }));
  root.appendChild(_row("Screen", (() => {
    const s = _select("ctl-screen", screenOpts, (e) => navigateTo(e.target.value));
    return s;
  })()));

  // -- Connection --
  root.appendChild(_el("h3", {}, "Connection"));
  root.appendChild(_row("Link", _select("link", [
    { v: "off", t: "Disconnected (N/C)" }, { v: "connecting", t: "Connecting" }, { v: "connected", t: "Connected" },
  ], applyLink)));
  root.appendChild(_row("Transport", _select("transport", [
    { v: "uart", t: "UART (cable)" }, { v: "wifi", t: "WiFi" },
  ], (e) => { _commsMode = e.target.value === "wifi" ? COMMS_MODE_WIFI : COMMS_MODE_UART; pendantMachine.port = _commsMode === COMMS_MODE_WIFI ? "WiFi:81" : "UART0"; repaint(); })));
  root.appendChild(_row("Status", _select("status", [
    "Idle", "Run", "Jog", "Hold:0", "Home", "Door:0", "Check", "Sleep", "Alarm:1", "Alarm:2", "Alarm:4", "Alarm:6", "N/C",
  ].map((s) => ({ v: s, t: s })), (e) => { pendantMachine.status = e.target.value; repaint(); })));

  // -- Machine --
  root.appendChild(_el("h3", {}, "Machine"));
  root.appendChild(_row("Axes", _select("axes", [{ v: "3", t: "3 (XYZ)" }, { v: "4", t: "4 (XYZA)" }],
    (e) => { pendantMachine.numAxes = +e.target.value; repaint(); })));
  root.appendChild(_row("Units", _select("units", [{ v: "mm", t: "mm (G21)" }, { v: "in", t: "inch (G20)" }],
    (e) => { pendantMachine.inInches = e.target.value === "in"; repaint(); })));
  root.appendChild(_row("WCS", _select("wcs", ["G54", "G55", "G56", "G57"].map((s, i) => ({ v: String(i), t: s })),
    (e) => { pendantProbing.selectedCoordIndex = +e.target.value; pendantProbing.selectedCoordSystem = ["G54", "G55", "G56", "G57"][+e.target.value]; repaint(); })));

  // -- Positions (machine / work) --
  root.appendChild(_el("h3", {}, "Position (work)"));
  for (const ax of ["X", "Y", "Z", "A"]) {
    root.appendChild(_row(ax, _num("pos" + ax, 0.1, (e) => { pendantMachine["pos" + ax] = +e.target.value || 0; repaint(); })));
  }
  root.appendChild(_el("h3", {}, "Position (machine)"));
  for (const ax of ["X", "Y", "Z", "A"]) {
    root.appendChild(_row(ax, _num("work" + ax, 0.1, (e) => { pendantMachine["work" + ax] = +e.target.value || 0; repaint(); })));
  }

  // -- Feeds / spindle --
  root.appendChild(_el("h3", {}, "Feeds / Spindle"));
  root.appendChild(_row("Feed rate", _num("feedRate", 10, (e) => { pendantMachine.feedRate = +e.target.value || 0; repaint(); })));
  root.appendChild(_row("Spindle RPM", _num("spindleRPM", 100, (e) => { pendantMachine.spindleRPM = +e.target.value || 0; repaint(); })));
  root.appendChild(_row("Max RPM", _num("spindleMaxRPM", 1000, (e) => { pendantMachine.spindleMaxRPM = +e.target.value || 0; repaint(); })));

  // -- Job --
  root.appendChild(_el("h3", {}, "Job"));
  root.appendChild(_row("Current file", (() => {
    const i = _el("input", { id: "currentFile", type: "text", oninput: (e) => { pendantMachine.currentFile = e.target.value; repaint(); } });
    _ctl["currentFile"] = i; return i;
  })()));
  root.appendChild(_row("Progress %", _range("jobPercent", 0, 100, 1, (e) => { pendantMachine.jobPercent = +e.target.value; repaint(); })));

  // -- Battery / WiFi (WiFi transport) --
  root.appendChild(_el("h3", {}, "Battery / WiFi (WiFi mode)"));
  root.appendChild(_row("Battery %", _range("battery", -1, 100, 1, (e) => { pendantMachine.batteryPercent = +e.target.value; repaint(); })));
  root.appendChild(_row("Charging", _check("charging", (e) => { pendantMachine.batteryCharging = e.target.checked; repaint(); })));
  root.appendChild(_row("Signal bars", _range("bars", -1, 4, 1, (e) => { pendantMachine.wifiSignalBars = +e.target.value; repaint(); })));
  root.appendChild(_row("AP mode", _check("apmode", (e) => { pendantMachine.wifiInApMode = e.target.checked; repaint(); })));
  root.appendChild(_row("SSID", (() => {
    const i = _el("input", { id: "ssid", type: "text", oninput: (e) => { pendantMachine.wifiSSID = e.target.value || "---"; repaint(); } });
    _ctl["ssid"] = i; return i;
  })()));
  root.appendChild(_row("FluidNC IP", (() => {
    const i = _el("input", { id: "ip", type: "text", oninput: (e) => { pendantMachine.ipAddress = e.target.value || "---"; repaint(); } });
    _ctl["ip"] = i; return i;
  })()));

  // -- Actions --
  root.appendChild(_el("h3", {}, "Actions"));
  const actions = _el("div", { class: "ctl-actions" }, [
    _el("button", { type: "button", onclick: () => { drawCurrentPendantScreen(); } }, "Redraw"),
    _el("button", { type: "button", onclick: () => { simLog.length = 0; renderLog(); } }, "Clear log"),
    _el("button", { type: "button", onclick: resetSimState }, "Reset state"),
  ]);
  root.appendChild(actions);

  syncControlsFromState();
}

// Push current state values into the control widgets (used on boot and after
// the UI itself changes state, e.g. encoder jog, transport toggle).
function syncControlsFromState() {
  const set = (id, v) => { if (_ctl[id]) _ctl[id].value = v; };
  const chk = (id, v) => { if (_ctl[id]) _ctl[id].checked = v; };
  set("ctl-screen", currentPendantScreen);
  set("link", pendantConnected ? "connected" : pendantMachine.connectionStatus === "Connecting" ? "connecting" : "off");
  set("transport", comms_active_mode() === COMMS_MODE_WIFI ? "wifi" : "uart");
  set("status", pendantMachine.status);
  set("axes", String(pendantMachine.numAxes));
  set("units", pendantMachine.inInches ? "in" : "mm");
  set("wcs", String(pendantProbing.selectedCoordIndex));
  for (const ax of ["X", "Y", "Z", "A"]) {
    set("pos" + ax, pendantMachine["pos" + ax]);
    set("work" + ax, pendantMachine["work" + ax]);
  }
  set("feedRate", pendantMachine.feedRate);
  set("spindleRPM", pendantMachine.spindleRPM);
  set("spindleMaxRPM", pendantMachine.spindleMaxRPM);
  set("currentFile", pendantMachine.currentFile);
  set("jobPercent", pendantMachine.jobPercent);
  set("battery", pendantMachine.batteryPercent);
  chk("charging", pendantMachine.batteryCharging);
  set("bars", pendantMachine.wifiSignalBars);
  chk("apmode", pendantMachine.wifiInApMode);
  set("ssid", pendantMachine.wifiSSID === "---" ? "" : pendantMachine.wifiSSID);
  set("ip", pendantMachine.ipAddress === "---" ? "" : pendantMachine.ipAddress);
}

function resetSimState() {
  localStorage.removeItem("sim.probe");
  localStorage.removeItem("sim.jog");
  sessionStorage.removeItem("sim.session");
  location.reload();
}
