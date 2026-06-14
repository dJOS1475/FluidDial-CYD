/*
 * syncbanner.js — polls the dev server's /__sync_status endpoint and shows a
 * banner when one or more firmware screen files have changed since the JS ports
 * were last reconciled (via `python3 simulator/sync.py --accept`).  So when you
 * edit the FluidDial-CYD firmware, the sim tells you exactly which JS port to
 * update.  No-ops on a plain static host (endpoint absent).
 */
(function () {
  const el = document.getElementById("syncbanner");
  if (!el) return;

  async function poll() {
    let r;
    try {
      r = await fetch("/__sync_status", { cache: "no-store" });
      if (!r.ok) throw 0;
    } catch (e) {
      el.style.display = "none"; // not served by the dev server
      return;
    }
    const s = await r.json();
    if (s.inSync || !s.changed || s.changed.length === 0) {
      el.style.display = "none";
      return;
    }
    const rows = s.changed
      .map((c) => `<li><code>${c.cpp}</code> &rarr; <code>${c.js}</code></li>`)
      .join("");
    el.innerHTML =
      `<strong>&#9888; ${s.changed.length} firmware file(s) changed</strong> since the JS ports were last synced ` +
      `&mdash; these sim screens may be out of date:` +
      `<ul>${rows}</ul>` +
      `<small>Update the JS port(s), then run <code>python3 simulator/sync.py --accept</code> to clear this. ` +
      `Colours regenerate automatically.</small>`;
    el.style.display = "block";
  }

  poll();
  setInterval(poll, 3000);
})();
