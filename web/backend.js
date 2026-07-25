// Backend abstraction for the 2048 ranks page.
//
// The same page runs in two modes:
// - "server": served by web/server.py — capture through mss, solver through
//   the native binary, sessions on disk. The historical local setup.
// - "browser": served as a static site — capture through getDisplayMedia,
//   solver through WebAssembly in a worker, sessions in localStorage.
//   Nothing ever leaves the machine.
//
// detectBackend() probes the server API and picks the right one. Both
// backends expose the same interface, consumed by app.js.

async function detectBackend() {
  try {
    const response = await fetch("/api/sessions");
    if (response.ok) {
      return createServerBackend();
    }
  } catch (error) {
    // No server API: static hosting.
  }
  return createLocalBackend();
}

// ---------------------------------------------------------------------------
// Server mode: thin wrappers over the existing HTTP API.

function createServerBackend() {
  let monitors = [];

  return {
    mode: "server",

    async loadSession(name) {
      const response = await fetch(`/api/session?name=${encodeURIComponent(name)}`);
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async saveSession(name, data) {
      const response = await fetch(`/api/session?name=${encodeURIComponent(name)}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(data),
      });
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async listSessions() {
      const response = await fetch("/api/sessions");
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async newSession(name) {
      const response = await fetch(`/api/new-session?name=${encodeURIComponent(name)}`, {
        method: "POST",
      });
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async estimateMoves(score) {
      const response = await fetch(`/api/estimate-moves?score=${encodeURIComponent(score)}`);
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async suggest(sessionName, _state, options, target) {
      const query = new URLSearchParams({
        name: sessionName,
        solver: options.solver,
        quality: options.quality,
        model_session: options.modelSession,
        model_stats: options.modelStats,
        target: String(target),
        timeout: options.timeout,
      });
      const response = await fetch(`/api/suggestion?${query.toString()}`);
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async captureStatus() {
      try {
        const response = await fetch("/api/capture/status");
        const payload = await response.json();
        monitors = Array.isArray(payload.monitors) ? payload.monitors : [];
        return {
          available: Boolean(payload.available),
          calibrated: Boolean(payload.config && payload.config.board),
          monitors,
          monitor: payload.config ? payload.config.monitor : 1,
          hint: payload.error || "",
        };
      } catch (error) {
        return { available: false, calibrated: false, monitors: [], monitor: 1, hint: "" };
      }
    },

    async prepareCapture() {},

    async calibrationFrameURL(monitor) {
      return `/api/capture/frame?source=monitor&monitor=${monitor}&t=${Date.now()}`;
    },

    async boardFrameURL() {
      return `/api/capture/frame?source=board&t=${Date.now()}`;
    },

    async saveCalibration(naturalRect, monitor) {
      const found = monitors.find((entry) => entry.index === monitor) || { left: 0, top: 0 };
      const body = {
        monitor,
        board: {
          left: Math.round(found.left + naturalRect.x),
          top: Math.round(found.top + naturalRect.y),
          width: Math.round(naturalRect.width),
          height: Math.round(naturalRect.height),
        },
        score: null,
      };
      const response = await fetch("/api/capture/region", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },

    async readBoard() {
      const response = await fetch("/api/capture/board");
      return response.json().then((payload) => ({ httpOk: response.ok, ...payload }));
    },
  };
}

// ---------------------------------------------------------------------------
// Browser mode: everything client-side.

function createLocalBackend() {
  const SESSION_PREFIX = "ranks2048.session.";
  const CAPTURE_KEY = "ranks2048.capture";

  const SEEDED_KEY = "ranks2048.seeded";

  let stream = null;
  let video = null;
  let templatesPromise = null;
  let seededPromise = null;
  let worker = null;
  let workerSeq = 0;
  const workerPending = new Map();

  function sanitizeName(name) {
    const safe = Array.from(name, (ch) => (/[a-zA-Z0-9_-]/.test(ch) ? ch : "_")).join("");
    return safe || "default";
  }

  function sessionKey(name) {
    return SESSION_PREFIX + sanitizeName(name);
  }

  function readStoredSession(name) {
    const text = localStorage.getItem(sessionKey(name));
    if (!text) {
      return null;
    }
    try {
      return JSON.parse(text);
    } catch (error) {
      return null;
    }
  }

  function allStoredSessions() {
    const sessions = {};
    for (let index = 0; index < localStorage.length; index += 1) {
      const key = localStorage.key(index);
      if (key && key.startsWith(SESSION_PREFIX)) {
        try {
          sessions[key.slice(SESSION_PREFIX.length)] = JSON.parse(localStorage.getItem(key));
        } catch (error) {
          // Ignore corrupted entries.
        }
      }
    }
    return sessions;
  }

  function loadCalibration() {
    try {
      const data = JSON.parse(localStorage.getItem(CAPTURE_KEY) || "null");
      if (data && data.board && data.board.width >= 8 && data.board.height >= 8) {
        return data;
      }
    } catch (error) {
      // Fall through.
    }
    return null;
  }

  // First visit: import the sessions bundled with the site, so the spawn
  // model starts from the project's real learned data instead of nothing.
  function ensureSeeded() {
    seededPromise ??= (async () => {
      if (localStorage.getItem(SEEDED_KEY)) {
        return;
      }
      try {
        const response = await fetch("seed-sessions.json");
        if (!response.ok) {
          return;
        }
        const sessions = await response.json();
        for (const [name, data] of Object.entries(sessions)) {
          if (!localStorage.getItem(sessionKey(name))) {
            localStorage.setItem(sessionKey(name), JSON.stringify(data));
          }
        }
      } catch (error) {
        // No seed bundle: nothing to import.
      } finally {
        localStorage.setItem(SEEDED_KEY, "1");
      }
    })();
    return seededPromise;
  }

  async function loadTemplates() {
    templatesPromise ??= fetch("digit_templates.json")
      .then((response) => response.json())
      .then((payload) => payload.digits);
    return templatesPromise;
  }

  async function ensureStream() {
    if (stream && stream.active) {
      return;
    }
    stream = await navigator.mediaDevices.getDisplayMedia({
      video: { frameRate: 10 },
      audio: false,
    });
    video = document.createElement("video");
    video.srcObject = stream;
    video.muted = true;
    await video.play();
    stream.getVideoTracks()[0].addEventListener("ended", () => {
      stream = null;
    });
  }

  function grabCanvas(rect = null) {
    if (!stream || !stream.active || !video || video.videoWidth === 0) {
      return null;
    }
    const source = rect || { x: 0, y: 0, width: video.videoWidth, height: video.videoHeight };
    const canvas = document.createElement("canvas");
    canvas.width = source.width;
    canvas.height = source.height;
    const context2d = canvas.getContext("2d", { willReadFrequently: true });
    context2d.drawImage(
      video,
      source.x, source.y, source.width, source.height,
      0, 0, source.width, source.height,
    );
    return canvas;
  }

  function ensureWorker() {
    if (worker) {
      return worker;
    }
    worker = new Worker("solver/solver-worker.mjs", { type: "module" });
    worker.onmessage = (event) => {
      const { id, result } = event.data;
      const resolve = workerPending.get(id);
      if (resolve) {
        workerPending.delete(id);
        resolve(result);
      }
    };
    worker.onerror = (event) => {
      const error = { ok: false, error: `Solver worker failed: ${event.message || "unknown error"}` };
      for (const resolve of workerPending.values()) {
        resolve(error);
      }
      workerPending.clear();
      worker.terminate();
      worker = null;
    };
    return worker;
  }

  return {
    mode: "browser",

    async loadSession(name) {
      await ensureSeeded();
      const data = readStoredSession(name);
      if (!data) {
        return { httpOk: false, error: `Session not found: ${sanitizeName(name)}` };
      }
      return { httpOk: true, name, path: "this browser's storage", data };
    },

    async saveSession(name, data) {
      localStorage.setItem(sessionKey(name), JSON.stringify(data));
      return { httpOk: true, ok: true, path: "this browser's storage" };
    },

    async listSessions() {
      await ensureSeeded();
      const sessions = Object.entries(allStoredSessions()).map(([name, data]) => ({
        name,
        score: Math.max(0, Number(data.score) || 0),
        moves: Math.max(0, Number(data.moves) || 0),
        highest: Array.isArray(data.cells) ? Math.max(0, ...data.cells.map(Number)) : 0,
        context_ready: Boolean(data.context_ready),
      }));
      sessions.sort((a, b) => a.name.localeCompare(b.name));
      return { httpOk: true, ok: true, sessions };
    },

    async newSession(name) {
      if (readStoredSession(name)) {
        return { httpOk: false, error: `Session already exists: ${sanitizeName(name)}` };
      }
      return { httpOk: true, ok: true };
    },

    async estimateMoves(score) {
      await ensureSeeded();
      const value = Math.max(0, Math.round(Number(score) || 0));
      if (value <= 0) {
        return { httpOk: true, ok: true, score: value, moves: 0 };
      }
      const refs = Object.values(allStoredSessions())
        .map((data) => ({ score: Number(data.score) || 0, moves: Number(data.moves) || 0 }))
        .filter((ref) => ref.score > 0 && ref.moves > 0);
      if (refs.length === 0) {
        return { httpOk: true, ok: true, score: value, moves: Math.round(value / 20) };
      }
      let weightedTotal = 0;
      let weightSum = 0;
      for (const ref of refs) {
        const ratioMoves = ref.moves * (value / ref.score);
        const distance = Math.abs(ref.score - value) / Math.max(value, ref.score);
        const weight = 1 / ((0.18 + distance) ** 2);
        weightedTotal += ratioMoves * weight;
        weightSum += weight;
      }
      return {
        httpOk: true,
        ok: true,
        score: value,
        moves: Math.max(0, Math.round(weightedTotal / weightSum)),
      };
    },

    async suggest(sessionName, state, options, target) {
      await ensureSeeded();
      const extraSessions = allStoredSessions();
      delete extraSessions[sanitizeName(sessionName)];
      const id = (workerSeq += 1);
      const result = await new Promise((resolve) => {
        workerPending.set(id, resolve);
        ensureWorker().postMessage({
          id,
          payload: {
            session: state,
            sessionName: sanitizeName(sessionName),
            extraSessions,
            options: {
              solver: options.solver,
              quality: options.quality,
              modelStats: options.modelStats,
              modelSession: options.modelSession,
              target,
            },
          },
        });
      });
      return { httpOk: Boolean(result.ok), ...result };
    },

    async captureStatus() {
      const available = Boolean(navigator.mediaDevices && navigator.mediaDevices.getDisplayMedia);
      return {
        available,
        calibrated: Boolean(loadCalibration()),
        monitors: [],
        monitor: 0,
        capturing: Boolean(stream && stream.active),
        hint: available ? "" : "This browser does not support screen capture (getDisplayMedia).",
      };
    },

    // Must be called from a user gesture (button click): the browser only
    // shows the window picker during transient activation.
    async prepareCapture() {
      await ensureStream();
    },

    async calibrationFrameURL() {
      await ensureStream();
      const canvas = grabCanvas();
      return canvas ? canvas.toDataURL("image/png") : "";
    },

    async boardFrameURL() {
      const calibration = loadCalibration();
      const canvas = calibration ? grabCanvas(calibration.board) : null;
      return canvas ? canvas.toDataURL("image/png") : "";
    },

    async saveCalibration(naturalRect) {
      const board = {
        x: Math.round(naturalRect.x),
        y: Math.round(naturalRect.y),
        width: Math.round(naturalRect.width),
        height: Math.round(naturalRect.height),
      };
      if (board.width < 8 || board.height < 8) {
        return { httpOk: false, error: "Invalid capture region." };
      }
      localStorage.setItem(CAPTURE_KEY, JSON.stringify({ board }));
      return { httpOk: true, ok: true };
    },

    async exportSessions() {
      await ensureSeeded();
      return { sessions: allStoredSessions() };
    },

    async importSessions(payload) {
      const sessions = payload && typeof payload.sessions === "object" ? payload.sessions : null;
      if (!sessions) {
        return { ok: false, error: "Invalid backup file: missing sessions." };
      }
      let count = 0;
      for (const [name, data] of Object.entries(sessions)) {
        if (data && typeof data === "object") {
          localStorage.setItem(sessionKey(name), JSON.stringify(data));
          count += 1;
        }
      }
      return { ok: true, count };
    },

    async readBoard() {
      const calibration = loadCalibration();
      if (!calibration) {
        return { httpOk: false, ok: false, error: "No calibrated board region. Calibrate first." };
      }
      if (!stream || !stream.active) {
        return { httpOk: false, ok: false, error: "Screen capture stopped. Click Watch game to share the game window again." };
      }
      const canvas = grabCanvas(calibration.board);
      if (!canvas) {
        return { httpOk: false, ok: false, error: "No frame available yet." };
      }
      const context2d = canvas.getContext("2d", { willReadFrequently: true });
      const image = context2d.getImageData(0, 0, canvas.width, canvas.height);
      const templates = await loadTemplates();
      const result = recognizeBoard(image, templates);
      const unreadable = result.cells.filter((cell) => cell === null).length;
      return { httpOk: true, ok: true, ...result, unreadable };
    },
  };
}
