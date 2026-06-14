/*
 * livereload.js — connects to dev_server.py's /__livereload SSE endpoint and
 * reloads the page when a simulator file changes.  If the page is served by a
 * plain static server (no such endpoint), it fails the first connection and
 * quietly gives up, so this file is harmless in any hosting setup.
 */
(function () {
  if (!window.EventSource) return;
  let tries = 0;
  function connect() {
    const es = new EventSource("/__livereload");
    es.onmessage = function (e) {
      if (e.data === "reload") location.reload();
    };
    es.onopen = function () {
      tries = 0;
      console.log("[livereload] connected");
    };
    es.onerror = function () {
      es.close();
      // Retry a few times (server restart), then stop to avoid noise on static hosts.
      if (++tries <= 3) setTimeout(connect, 1000);
    };
  }
  connect();
})();
