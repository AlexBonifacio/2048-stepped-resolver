const SIZE = 4;

const params = new URLSearchParams(window.location.search);
const sessionName = params.get("session") || "try2";

const boardEl = document.querySelector("#board");
const statusEl = document.querySelector("#status");
const scoreEl = document.querySelector("#score");
const movesEl = document.querySelector("#moves");
const highestEl = document.querySelector("#highest");
const sessionNameEl = document.querySelector("#sessionName");
const existingSessionSelectEl = document.querySelector("#existingSessionSelect");
const openSessionButton = document.querySelector("#openSessionButton");
const newSessionNameEl = document.querySelector("#newSessionName");
const newSessionButton = document.querySelector("#newSessionButton");
const undoButton = document.querySelector("#undoButton");
const commitSpawnButton = document.querySelector("#commitSpawnButton");
const suggestionDirectionEl = document.querySelector("#suggestionDirection");
const suggestionMetaEl = document.querySelector("#suggestionMeta");
const suggestionRefreshButton = document.querySelector("#suggestionRefreshButton");
const suggestionRankingEl = document.querySelector("#suggestionRanking");
const contextControlsEl = document.querySelector(".context-controls");
const contextScoreEl = document.querySelector("#contextScore");
const applyContextButton = document.querySelector("#applyContextButton");

let state = null;
let history = [];
let pendingSpawn = null;
let suggestionRequestId = 0;

const suggestionOptions = {
  solver: params.get("solver") || "hybrid",
  quality: params.get("quality") || "godlike",
  modelSession: params.get("model_session") || "",
  modelStats: params.get("model_stats") || "sessions",
  timeout: params.get("timeout") || "90",
};
const defaultTargetRank = Math.max(1, Number(params.get("target")) || 12);

sessionNameEl.textContent = sessionName;

function directionLabel(direction) {
  return {
    haut: "up",
    bas: "down",
    gauche: "left",
    droite: "right",
    up: "up",
    down: "down",
    left: "left",
    right: "right",
  }[direction] || direction;
}

function emptySession() {
  return {
    moves: 0,
    score: 0,
    cells: Array(SIZE * SIZE).fill(0),
    map: Array.from({ length: SIZE }, () => Array(SIZE).fill(0)),
    observations: [],
    spawns: {},
    solved: [],
    context_ready: false,
    outcome: {
      status: "in_progress",
      target: defaultTargetRank,
    },
  };
}

function normalizeOutcome(outcome) {
  const target = Math.max(1, Number(outcome && outcome.target) || defaultTargetRank);
  if (!outcome || typeof outcome !== "object") {
    return { status: "in_progress", target };
  }
  return {
    status: outcome.status === "ended" ? "ended" : "in_progress",
    target,
    final_score: Math.max(0, Number(outcome.final_score) || 0),
    final_moves: Math.max(0, Number(outcome.final_moves) || 0),
    final_highest: Math.max(0, Number(outcome.final_highest) || 0),
    success: Boolean(outcome.success),
    reason: typeof outcome.reason === "string" ? outcome.reason : "",
    ended_at: typeof outcome.ended_at === "string" ? outcome.ended_at : "",
  };
}

function normalizeSession(data) {
  const next = { ...emptySession(), ...data };
  if (Array.isArray(data.cells) && data.cells.length >= SIZE * SIZE) {
    next.cells = data.cells.slice(0, SIZE * SIZE).map((value) => Math.max(0, Number(value) || 0));
  } else if (Array.isArray(data.map)) {
    next.cells = data.map.flat().slice(0, SIZE * SIZE).map((value) => Math.max(0, Number(value) || 0));
  }
  next.moves = Math.max(0, Number(next.moves) || 0);
  next.score = Math.max(0, Number(next.score) || 0);
  next.observations = Array.isArray(next.observations) ? next.observations : [];
  next.spawns = next.spawns && typeof next.spawns === "object" ? next.spawns : {};
  next.solved = Array.isArray(next.solved)
    ? next.solved.map((move) => ({
      ...move,
      source: typeof move.source === "string" ? move.source : "human",
    }))
    : [];
  next.context_ready = Boolean(next.context_ready) ||
    next.moves > 0 ||
    next.score > 0 ||
    next.observations.length > 0 ||
    next.solved.length > 0;
  next.outcome = normalizeOutcome(next.outcome);
  syncMap(next);
  return next;
}

function syncMap(session) {
  session.map = [];
  for (let row = 0; row < SIZE; row += 1) {
    session.map.push(session.cells.slice(row * SIZE, row * SIZE + SIZE));
  }
}

function snapshot() {
  return JSON.parse(JSON.stringify({ state, pendingSpawn }));
}

function restore(snap) {
  state = snap.state;
  pendingSpawn = snap.pendingSpawn;
  syncContextInputs();
  render();
}

function pushHistory() {
  history.push(snapshot());
  if (history.length > 100) {
    history.shift();
  }
}

function rankPoints(rank) {
  return 2 ** rank;
}

function highestRank(cells = state.cells) {
  return cells.reduce((max, value) => Math.max(max, value), 0);
}

function readLine(cells, direction, index) {
  const line = [];
  for (let offset = 0; offset < SIZE; offset += 1) {
    if (direction === "left") line.push(cells[index * SIZE + offset]);
    if (direction === "right") line.push(cells[index * SIZE + (SIZE - 1 - offset)]);
    if (direction === "up") line.push(cells[offset * SIZE + index]);
    if (direction === "down") line.push(cells[(SIZE - 1 - offset) * SIZE + index]);
  }
  return line;
}

function writeLine(cells, direction, index, line) {
  for (let offset = 0; offset < SIZE; offset += 1) {
    if (direction === "left") cells[index * SIZE + offset] = line[offset];
    if (direction === "right") cells[index * SIZE + (SIZE - 1 - offset)] = line[offset];
    if (direction === "up") cells[offset * SIZE + index] = line[offset];
    if (direction === "down") cells[(SIZE - 1 - offset) * SIZE + index] = line[offset];
  }
}

function mergeLine(line) {
  const compact = line.filter((value) => value !== 0);
  const result = [];
  let gained = 0;
  for (let index = 0; index < compact.length; index += 1) {
    if (index + 1 < compact.length && compact[index] === compact[index + 1]) {
      const merged = compact[index] + 1;
      result.push(merged);
      gained += rankPoints(merged);
      index += 1;
    } else {
      result.push(compact[index]);
    }
  }
  while (result.length < SIZE) {
    result.push(0);
  }
  return { line: result, gained };
}

function applyMove(direction) {
  const cells = state.cells.slice();
  let changed = false;
  let gained = 0;

  for (let index = 0; index < SIZE; index += 1) {
    const before = readLine(cells, direction, index);
    const merged = mergeLine(before);
    if (before.some((value, offset) => value !== merged.line[offset])) {
      changed = true;
      writeLine(cells, direction, index, merged.line);
      gained += merged.gained;
    }
  }

  return { changed, cells, gained };
}

function canMove() {
  return ["left", "right", "up", "down"].some((direction) => applyMove(direction).changed);
}

function currentOutcome(status = "ended", reason = "manual") {
  const target = defaultTargetRank;
  const finalHighest = highestRank();
  return {
    status,
    target,
    final_score: state.score,
    final_moves: state.moves,
    final_highest: finalHighest,
    success: finalHighest >= target,
    reason,
    ended_at: status === "ended" ? new Date().toISOString() : "",
  };
}

function recordSolvedMove(direction, beforeCells, result, context) {
  state.solved.push({
    direction,
    source: "human",
    moves: context.moves,
    after_moves: context.moves + 1,
    score: context.score,
    highest: context.highest,
    gained: result.gained,
    before: beforeCells,
    after: result.cells.slice(),
  });
}

function isInitialSetup() {
  return state &&
    !pendingSpawn &&
    state.moves === 0 &&
    state.score === 0 &&
    state.observations.length === 0;
}

function gameHasStarted() {
  return Boolean(state) && (
    state.context_ready ||
    state.moves > 0 ||
    state.score > 0 ||
    state.observations.length > 0 ||
    state.solved.length > 0 ||
    pendingSpawn
  );
}

function scoreIsFilled() {
  return contextScoreEl.value.trim() !== "";
}

function requireContextReady() {
  if (state?.context_ready) {
    return true;
  }
  setStatus("Enter the current score and press Start before playing.", "warn");
  contextScoreEl.focus();
  return false;
}

function move(direction) {
  if (isWatching()) {
    setStatus("Watch mode is on: play in the game, the board updates by itself.", "warn");
    return;
  }
  if (!requireContextReady()) {
    return;
  }
  if (!state || pendingSpawn) {
    setStatus("Place the new tile first, or clear it with right click.", "warn");
    return;
  }

  const beforeHighest = highestRank();
  const beforeCells = state.cells.slice();
  const context = {
    moves: state.moves,
    score: state.score,
    highest: beforeHighest,
  };
  const result = applyMove(direction);
  if (!result.changed) {
    setStatus("No tile moves in that direction.", "warn");
    return;
  }

  pushHistory();
  recordSolvedMove(direction, beforeCells, result, context);
  state.cells = result.cells;
  state.score += result.gained;
  state.moves += 1;
  syncMap(state);
  pendingSpawn = { index: null, rank: 1, highestBeforeSpawn: beforeHighest };
  setStatus("Click an empty cell for the new tile. Left click raises it, right click lowers it to empty.");
  setSuggestion("...", "Place the new tile");
  render();
}

async function handleCellClick(index, event) {
  event.preventDefault();
  if (isWatching()) {
    setStatus("Watch mode is on: play in the game, the board updates by itself.", "warn");
    return;
  }
  if (isInitialSetup()) {
    pushHistory();
    const delta = event.type === "contextmenu" ? -1 : 1;
    state.cells[index] = Math.max(0, state.cells[index] + delta);
    syncMap(state);
    render();
    await saveSession();
    if (state.context_ready) {
      await refreshSuggestion();
    } else {
      setSuggestion("...", "");
      setSuggestionRanking();
      setStatus("Enter the current score and press Start before playing.");
    }
    return;
  }

  if (!pendingSpawn) {
    setStatus("Play a move with the arrow keys before placing a new tile.", "warn");
    return;
  }

  if (pendingSpawn.index === null) {
    if (state.cells[index] !== 0) {
      setStatus("That cell already existed, so it stays locked.", "warn");
      return;
    }
    pendingSpawn.index = index;
    pendingSpawn.rank = event.type === "contextmenu" ? 0 : 1;
  } else if (pendingSpawn.index !== index) {
    setStatus("Only the new tile can be edited before the move is saved.", "warn");
    return;
  } else {
    pendingSpawn.rank += event.type === "contextmenu" ? -1 : 1;
    pendingSpawn.rank = Math.max(0, pendingSpawn.rank);
  }

  render();
}

async function commitPendingSpawn({ save = true, refresh = true } = {}) {
  if (!pendingSpawn || pendingSpawn.index === null) {
    setStatus("Choose an empty cell before playing the next arrow key.", "warn");
    return false;
  }
  const rank = pendingSpawn.rank;
  if (rank > 0) {
    state.cells[pendingSpawn.index] = rank;
    state.observations.push({
      value: rank,
      moves: state.moves,
      highest: pendingSpawn.highestBeforeSpawn,
    });
    state.spawns[String(rank)] = (Number(state.spawns[String(rank)]) || 0) + 1;
  }
  pendingSpawn = null;
  syncMap(state);
  const ended = !canMove();
  if (ended) {
    state.outcome = currentOutcome("ended", "no_moves");
  }
  render();
  if (save) {
    await saveSession();
    if (refresh) {
      await refreshSuggestion();
    }
  }
  if (ended) {
    setStatus("Game over. The session has been saved.", "warn");
  }
  return true;
}

async function saveSession() {
  if (pendingSpawn) {
    setStatus("Confirm the new tile first; it will be saved with the move.", "warn");
    return;
  }

  syncMap(state);
  const response = await fetch(`/api/session?name=${encodeURIComponent(sessionName)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(state),
  });
  const payload = await response.json();
  if (!response.ok) {
    setStatus(payload.error || "Could not save.", "warn");
    return;
  }
}

function syncContextInputs() {
  if (!state) {
    contextScoreEl.value = "";
    return;
  }
  contextScoreEl.value = state.score > 0 ? String(state.score) : "";
}

async function loadSession() {
  const response = await fetch(`/api/session?name=${encodeURIComponent(sessionName)}`);
  const payload = await response.json();
  if (!response.ok) {
    state = emptySession();
    setStatus(payload.error || "Session not found.", "warn");
  } else {
    state = normalizeSession(payload.data);
    setStatus(`Session loaded from ${payload.path}.`);
  }
  history = [];
  pendingSpawn = null;
  syncContextInputs();
  render();
  if (state.context_ready) {
    await refreshSuggestion();
  } else {
    setSuggestion("...", "");
    setSuggestionRanking();
    setStatus("Copy the board, enter the current score, then press Start.");
  }
}

function sessionOptionLabel(session) {
  const details = [];
  if (Number(session.score) > 0) details.push(`score ${session.score}`);
  if (Number(session.moves) > 0) details.push(`${session.moves} moves`);
  if (Number(session.highest) > 0) details.push(`max ${session.highest}`);
  return details.length > 0 ? `${session.name} (${details.join(", ")})` : session.name;
}

async function loadSessionList() {
  try {
    const response = await fetch("/api/sessions");
    const payload = await response.json();
    if (!response.ok || !payload.ok) {
      return;
    }

    existingSessionSelectEl.replaceChildren();
    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Open session...";
    existingSessionSelectEl.append(placeholder);

    payload.sessions.forEach((session) => {
      const option = document.createElement("option");
      option.value = session.name;
      option.textContent = sessionOptionLabel(session);
      option.selected = session.name === sessionName;
      existingSessionSelectEl.append(option);
    });
    openSessionButton.disabled = !existingSessionSelectEl.value || existingSessionSelectEl.value === sessionName;
  } catch (error) {
    openSessionButton.disabled = true;
  }
}

async function estimateMoves(score) {
  if (score === 0) {
    return 0;
  }

  try {
    const response = await fetch(`/api/estimate-moves?score=${encodeURIComponent(score)}`);
    const payload = await response.json();
    if (!response.ok || !payload.ok) {
      throw new Error(payload.error || "Could not estimate moves.");
    }
    return Math.max(0, Math.round(Number(payload.moves) || 0));
  } catch (error) {
    setStatus("Move estimate server is unavailable; starting with 0 estimated moves.", "warn");
    return 0;
  }
}

async function applyContext() {
  if (!state || pendingSpawn) {
    setStatus("Confirm the current tile before changing the context.", "warn");
    return;
  }
  if (!scoreIsFilled()) {
    setStatus("Current score is required before starting.", "warn");
    contextScoreEl.focus();
    return;
  }

  const score = Math.max(0, Math.round(Number(contextScoreEl.value) || 0));
  const moves = await estimateMoves(score);
  pushHistory();
  state.score = score;
  state.moves = moves;
  state.context_ready = true;
  if (state.outcome?.status === "ended") {
    state.outcome = currentOutcome("ended", state.outcome.reason || "manual");
  }
  syncContextInputs();
  render();
  await saveSession();
  await refreshSuggestion();
  setStatus(`Started with score ${score}.`);
}

async function createNewSession() {
  const rawName = newSessionNameEl.value.trim();
  if (!rawName) {
    setStatus("Give the new session a name.", "warn");
    newSessionNameEl.focus();
    return;
  }

  const response = await fetch(`/api/new-session?name=${encodeURIComponent(rawName)}`, {
    method: "POST",
  });
  const payload = await response.json();
  if (!response.ok) {
    setStatus(payload.error || "Could not create the session.", "warn");
    return;
  }

  window.location.href = `/?session=${encodeURIComponent(rawName)}`;
}

function openSelectedSession() {
  const selected = existingSessionSelectEl.value;
  if (!selected || selected === sessionName) {
    return;
  }
  window.location.href = `/?session=${encodeURIComponent(selected)}`;
}

function setStatus(text, mode = "") {
  statusEl.textContent = text;
  statusEl.className = `status ${mode}`.trim();
}

function setSuggestion(direction, meta, mode = "") {
  suggestionDirectionEl.textContent = directionLabel(direction);
  suggestionMetaEl.textContent = meta;
  suggestionMetaEl.className = mode;
}

function setSuggestionRanking(ranking = []) {
  suggestionRankingEl.replaceChildren();
  ranking.forEach((item, index) => {
    const entry = document.createElement("li");
    const direction = document.createElement("strong");
    const score = document.createElement("span");
    direction.textContent = `${index + 1}. ${directionLabel(item.direction)}`;
    score.textContent = Number(item.score).toLocaleString("en-US", { maximumFractionDigits: 0 });
    entry.append(direction, score);
    suggestionRankingEl.append(entry);
  });
}

async function refreshSuggestion() {
  const requestId = ++suggestionRequestId;
  if (!state) {
    setSuggestion("...", "Session not loaded");
    setSuggestionRanking();
    return;
  }
  if (!requireContextReady()) {
    setSuggestion("...", "");
    setSuggestionRanking();
    return;
  }
  if (pendingSpawn) {
    setSuggestion("...", "Place the new tile");
    setSuggestionRanking();
    return;
  }
  if (!canMove()) {
    setSuggestion("end", "No move available", "warn");
    setSuggestionRanking();
    return;
  }

  suggestionRefreshButton.disabled = true;
  setSuggestion("...", "AI is thinking");
  setSuggestionRanking();
  const query = new URLSearchParams({
    name: sessionName,
    solver: suggestionOptions.solver,
    quality: suggestionOptions.quality,
    model_session: suggestionOptions.modelSession,
    model_stats: suggestionOptions.modelStats,
    target: String(defaultTargetRank),
    timeout: suggestionOptions.timeout,
  });

  try {
    const response = await fetch(`/api/suggestion?${query.toString()}`);
    const payload = await response.json();
    if (requestId !== suggestionRequestId) {
      return;
    }
    if (!response.ok || !payload.ok) {
      setSuggestion("!", payload.error || "Could not calculate", "warn");
      return;
    }
    setSuggestion(payload.direction, "");
    setSuggestionRanking(payload.ranking || []);
  } catch (error) {
    if (requestId === suggestionRequestId) {
      setSuggestion("!", "AI server unavailable", "warn");
      setSuggestionRanking();
    }
  } finally {
    if (requestId === suggestionRequestId) {
      suggestionRefreshButton.disabled = Boolean(pendingSpawn);
    }
  }
}

function rankClass(rank) {
  if (rank === 0) return "";
  if (rank > 11) return "rank-high";
  return `rank-${rank}`;
}

function render() {
  scoreEl.textContent = state.score;
  movesEl.textContent = state.moves;
  highestEl.textContent = highestRank();
  undoButton.disabled = history.length === 0;
  commitSpawnButton.disabled = !pendingSpawn;
  suggestionRefreshButton.disabled = Boolean(pendingSpawn);
  contextControlsEl.classList.toggle("is-hidden", gameHasStarted());
  applyContextButton.disabled = Boolean(pendingSpawn) || !scoreIsFilled();

  boardEl.replaceChildren();
  state.cells.forEach((rank, index) => {
    const button = document.createElement("button");
    const pendingHere = pendingSpawn && pendingSpawn.index === index;
    const shownRank = pendingHere ? pendingSpawn.rank : rank;
    button.type = "button";
    button.className = [
      "cell",
      shownRank === 0 ? "empty" : "occupied",
      rank !== 0 && !pendingHere ? "locked" : "",
      pendingHere ? "pending" : "",
      rankClass(shownRank),
    ].filter(Boolean).join(" ");
    button.setAttribute("role", "gridcell");
    button.textContent = shownRank === 0 ? "" : shownRank;
    button.addEventListener("click", (event) => {
      void handleCellClick(index, event);
    });
    button.addEventListener("contextmenu", (event) => {
      void handleCellClick(index, event);
    });
    boardEl.append(button);
  });
}

document.addEventListener("keydown", async (event) => {
  const editingField = ["INPUT", "TEXTAREA", "SELECT"].includes(event.target.tagName);
  if (event.key === "Enter" && pendingSpawn && !editingField) {
    event.preventDefault();
    await commitPendingSpawn();
    return;
  }
  if (event.key === "Enter" && !editingField) {
    event.preventDefault();
    await refreshSuggestion();
    return;
  }

  const direction = {
    ArrowLeft: "left",
    ArrowRight: "right",
    ArrowUp: "up",
    ArrowDown: "down",
  }[event.key];

  if (direction) {
    event.preventDefault();
    if (pendingSpawn) {
      const committed = await commitPendingSpawn({ refresh: false });
      if (!committed) {
        return;
      }
    }
    move(direction);
  }
});

undoButton.addEventListener("click", async () => {
  const snap = history.pop();
  if (!snap) {
    return;
  }
  restore(snap);
  if (!pendingSpawn) {
    await saveSession();
    await refreshSuggestion();
  } else {
    setStatus("Undo applied. Place the new tile when ready.");
    setSuggestion("...", "Place the new tile");
  }
});

commitSpawnButton.addEventListener("click", async () => {
  await commitPendingSpawn();
});
suggestionRefreshButton.addEventListener("click", refreshSuggestion);
applyContextButton.addEventListener("click", applyContext);
contextScoreEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    applyContext();
  }
});
contextScoreEl.addEventListener("input", () => {
  applyContextButton.disabled = !scoreIsFilled();
});
newSessionButton.addEventListener("click", createNewSession);
openSessionButton.addEventListener("click", openSelectedSession);
existingSessionSelectEl.addEventListener("change", () => {
  openSessionButton.disabled = !existingSessionSelectEl.value || existingSessionSelectEl.value === sessionName;
});
existingSessionSelectEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    openSelectedSession();
  }
});
newSessionNameEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    createNewSession();
  }
});

// --- Screen capture calibration ---

const captureStatusEl = document.querySelector("#captureStatus");
const calibrateButton = document.querySelector("#calibrateButton");
const capturePreviewButton = document.querySelector("#capturePreviewButton");
const readBoardButton = document.querySelector("#readBoardButton");
const watchButton = document.querySelector("#watchButton");
const capturePreviewImageEl = document.querySelector("#capturePreviewImage");
const captureModalEl = document.querySelector("#captureModal");
const captureModalHintEl = document.querySelector("#captureModalHint");
const captureMonitorSelectEl = document.querySelector("#captureMonitorSelect");
const captureRefreshButton = document.querySelector("#captureRefreshButton");
const captureSkipButton = document.querySelector("#captureSkipButton");
const captureCancelButton = document.querySelector("#captureCancelButton");
const captureStageEl = document.querySelector("#captureStage");
const captureModalImageEl = document.querySelector("#captureModalImage");
const captureBoardRectEl = document.querySelector("#captureBoardRect");
const captureDragRectEl = document.querySelector("#captureDragRect");

let captureAvailable = false;
let captureMonitors = [];
let captureConfig = null;
let calibration = null;
let capturePreviewTimer = null;

function captureBoardCalibrated() {
  return Boolean(captureConfig && captureConfig.board);
}

function renderCaptureStatus(message = "", mode = "") {
  calibrateButton.disabled = !captureAvailable;
  capturePreviewButton.disabled = !captureAvailable || !captureBoardCalibrated();
  readBoardButton.disabled = !captureAvailable || !captureBoardCalibrated();
  watchButton.disabled = !captureAvailable || !captureBoardCalibrated();
  if (watchButton.disabled) {
    stopWatch();
  }
  if (message) {
    captureStatusEl.textContent = message;
    captureStatusEl.className = `capture-status ${mode}`.trim();
    return;
  }
  if (!captureAvailable) {
    captureStatusEl.textContent = "Screen capture is unavailable. Install the mss package on the server: pip install mss";
    captureStatusEl.className = "capture-status warn";
    return;
  }
  if (captureBoardCalibrated()) {
    const board = captureConfig.board;
    const score = captureConfig.score ? ", score region set" : "";
    captureStatusEl.textContent = `Board region calibrated (${board.width}x${board.height} px${score}).`;
    captureStatusEl.className = "capture-status";
    return;
  }
  captureStatusEl.textContent = "Not calibrated yet. Click Calibrate, then drag a rectangle around the game grid.";
  captureStatusEl.className = "capture-status";
}

async function refreshCaptureStatus() {
  try {
    const response = await fetch("/api/capture/status");
    const payload = await response.json();
    captureAvailable = Boolean(payload.available);
    captureMonitors = Array.isArray(payload.monitors) ? payload.monitors : [];
    captureConfig = payload.config || null;
  } catch (error) {
    captureAvailable = false;
    captureMonitors = [];
  }
  renderCaptureStatus();
}

function captureMonitorLabel(monitor) {
  if (monitor.all) {
    return `All monitors (${monitor.width}x${monitor.height})`;
  }
  return `Monitor ${monitor.index} (${monitor.width}x${monitor.height})`;
}

function populateCaptureMonitors(selected) {
  captureMonitorSelectEl.replaceChildren();
  captureMonitors.forEach((monitor) => {
    const option = document.createElement("option");
    option.value = String(monitor.index);
    option.textContent = captureMonitorLabel(monitor);
    option.selected = monitor.index === selected;
    captureMonitorSelectEl.append(option);
  });
}

function setCalibrationStep(step) {
  calibration.step = step;
  captureSkipButton.hidden = step !== "score";
  captureModalHintEl.textContent = step === "board"
    ? "Drag a rectangle around the 4x4 grid."
    : "Now drag a rectangle around the score number, or click Skip score.";
}

async function loadCalibrationFrame() {
  captureModalImageEl.removeAttribute("src");
  captureBoardRectEl.hidden = true;
  captureDragRectEl.hidden = true;
  calibration.board = null;
  calibration.drag = null;
  setCalibrationStep("board");
  captureModalImageEl.src = `/api/capture/frame?source=monitor&monitor=${calibration.monitor}&t=${Date.now()}`;
}

async function openCalibration() {
  await refreshCaptureStatus();
  if (!captureAvailable) {
    return;
  }
  stopCapturePreview();
  stopWatch();
  calibration = {
    monitor: captureConfig?.monitor ?? 1,
    step: "board",
    board: null,
    drag: null,
  };
  if (!captureMonitors.some((monitor) => monitor.index === calibration.monitor)) {
    calibration.monitor = captureMonitors.length > 1 ? 1 : 0;
  }
  populateCaptureMonitors(calibration.monitor);
  captureModalEl.hidden = false;
  await loadCalibrationFrame();
}

function closeCalibration() {
  calibration = null;
  captureModalEl.hidden = true;
}

function calibrationPoint(event) {
  const bounds = captureModalImageEl.getBoundingClientRect();
  return {
    x: Math.min(Math.max(event.clientX - bounds.left, 0), bounds.width),
    y: Math.min(Math.max(event.clientY - bounds.top, 0), bounds.height),
  };
}

function dragRect(drag, point) {
  return {
    x: Math.min(drag.x, point.x),
    y: Math.min(drag.y, point.y),
    width: Math.abs(point.x - drag.x),
    height: Math.abs(point.y - drag.y),
  };
}

function placeRect(element, rect) {
  element.style.left = `${rect.x}px`;
  element.style.top = `${rect.y}px`;
  element.style.width = `${rect.width}px`;
  element.style.height = `${rect.height}px`;
  element.hidden = false;
}

function toScreenRect(rect) {
  const scaleX = captureModalImageEl.naturalWidth / captureModalImageEl.clientWidth;
  const scaleY = captureModalImageEl.naturalHeight / captureModalImageEl.clientHeight;
  const monitor = captureMonitors.find((entry) => entry.index === calibration.monitor) || { left: 0, top: 0 };
  return {
    left: Math.round(monitor.left + rect.x * scaleX),
    top: Math.round(monitor.top + rect.y * scaleY),
    width: Math.round(rect.width * scaleX),
    height: Math.round(rect.height * scaleY),
  };
}

async function saveCalibration(scoreRect) {
  const body = {
    monitor: calibration.monitor,
    board: toScreenRect(calibration.board),
    score: scoreRect ? toScreenRect(scoreRect) : null,
  };
  try {
    const response = await fetch("/api/capture/region", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const payload = await response.json();
    if (!response.ok || !payload.ok) {
      renderCaptureStatus(payload.error || "Could not save the capture region.", "warn");
      closeCalibration();
      return;
    }
    captureConfig = payload.config;
  } catch (error) {
    renderCaptureStatus("Capture server unavailable.", "warn");
    closeCalibration();
    return;
  }
  closeCalibration();
  renderCaptureStatus();
  setStatus("Screen capture calibrated.");
}

function handleCalibrationRect(rect) {
  if (calibration.step === "board") {
    calibration.board = rect;
    placeRect(captureBoardRectEl, rect);
    setCalibrationStep("score");
    return;
  }
  void saveCalibration(rect);
}

async function readBoardFromCapture() {
  if (!captureBoardCalibrated()) {
    return;
  }
  readBoardButton.disabled = true;
  try {
    const response = await fetch("/api/capture/board");
    const payload = await response.json();
    if (!response.ok || !payload.ok) {
      setStatus(payload.error || "Could not read the board.", "warn");
      return;
    }
    if (payload.unreadable > 0) {
      setStatus(`Could not read ${payload.unreadable} cell(s). Check the calibration with Preview.`, "warn");
      return;
    }
    if (!isInitialSetup()) {
      setStatus("Board reading is only available before the game starts, to copy the initial board.", "warn");
      return;
    }
    pushHistory();
    state.cells = payload.cells.map((value) => Math.max(0, Number(value) || 0));
    syncMap(state);
    render();
    await saveSession();
    setStatus("Board read from the game screen. Enter the current score, then press Start.");
  } catch (error) {
    setStatus("Capture server unavailable.", "warn");
  } finally {
    readBoardButton.disabled = !captureAvailable || !captureBoardCalibrated();
  }
}

// --- Watch mode: the game screen is the only source of truth. Read the
// board, fill the page, let the AI suggest, repeat. No move/spawn tracking.

const WATCH_INTERVAL_MS = 600;

let watchTimer = null;
let watchBusy = false;
let watchCandidateSignature = "";
let watchHasApplied = false;

function isWatching() {
  return watchTimer !== null;
}

function boardSignature(cells) {
  return cells.join(",");
}

function startWatch() {
  if (isWatching()) {
    return;
  }
  watchCandidateSignature = "";
  watchHasApplied = false;
  watchTimer = setInterval(() => {
    void watchTick();
  }, WATCH_INTERVAL_MS);
  watchButton.textContent = "Stop watching";
  setStatus("Watching the game screen...");
}

function stopWatch() {
  if (!isWatching()) {
    return;
  }
  clearInterval(watchTimer);
  watchTimer = null;
  watchButton.textContent = "Watch game";
  setStatus("Stopped watching the game screen.");
}

async function watchTick() {
  if (watchBusy || !state) {
    return;
  }
  watchBusy = true;
  try {
    const response = await fetch("/api/capture/board");
    const payload = await response.json();
    if (!response.ok || !payload.ok || payload.unreadable > 0) {
      return;
    }
    const cells = payload.cells.map((value) => Math.max(0, Number(value) || 0));
    if (!cells.some((value) => value !== 0)) {
      // Empty board: menu or popup probably covers the grid.
      return;
    }
    const signature = boardSignature(cells);
    if (signature !== watchCandidateSignature) {
      // Wait for two identical reads so animations settle.
      watchCandidateSignature = signature;
      return;
    }
    if (signature === boardSignature(state.cells)) {
      return;
    }
    await applyWatchedBoard(cells);
  } catch (error) {
    // Server hiccup: keep watching.
  } finally {
    watchBusy = false;
  }
}

async function applyWatchedBoard(cells) {
  pushHistory();
  pendingSpawn = null;
  state.cells = cells;
  if (state.context_ready && watchHasApplied) {
    state.moves += 1;
  }
  watchHasApplied = true;
  syncMap(state);
  if (!canMove()) {
    state.outcome = currentOutcome("ended", "no_moves");
  }
  render();
  await saveSession();
  if (state.context_ready) {
    await refreshSuggestion();
  } else {
    setStatus("Board read from the game screen. Enter the current score, then press Start.");
  }
}

function stopCapturePreview() {
  if (capturePreviewTimer !== null) {
    clearInterval(capturePreviewTimer);
    capturePreviewTimer = null;
  }
  capturePreviewImageEl.hidden = true;
  capturePreviewButton.textContent = "Preview";
}

function startCapturePreview() {
  const refreshFrame = () => {
    capturePreviewImageEl.src = `/api/capture/frame?source=board&t=${Date.now()}`;
  };
  refreshFrame();
  capturePreviewImageEl.hidden = false;
  capturePreviewButton.textContent = "Stop preview";
  capturePreviewTimer = setInterval(refreshFrame, 1000);
}

captureStageEl.addEventListener("pointerdown", (event) => {
  if (!calibration || !captureModalImageEl.naturalWidth) {
    return;
  }
  event.preventDefault();
  captureStageEl.setPointerCapture(event.pointerId);
  calibration.drag = calibrationPoint(event);
  placeRect(captureDragRectEl, dragRect(calibration.drag, calibration.drag));
});

captureStageEl.addEventListener("pointermove", (event) => {
  if (!calibration || !calibration.drag) {
    return;
  }
  placeRect(captureDragRectEl, dragRect(calibration.drag, calibrationPoint(event)));
});

captureStageEl.addEventListener("pointerup", (event) => {
  if (!calibration || !calibration.drag) {
    return;
  }
  const rect = dragRect(calibration.drag, calibrationPoint(event));
  calibration.drag = null;
  captureDragRectEl.hidden = true;
  if (rect.width < 8 || rect.height < 8) {
    return;
  }
  handleCalibrationRect(rect);
});

calibrateButton.addEventListener("click", () => {
  void openCalibration();
});
captureCancelButton.addEventListener("click", closeCalibration);
captureSkipButton.addEventListener("click", () => {
  if (calibration && calibration.board) {
    void saveCalibration(null);
  }
});
captureRefreshButton.addEventListener("click", () => {
  if (calibration) {
    void loadCalibrationFrame();
  }
});
captureMonitorSelectEl.addEventListener("change", () => {
  if (calibration) {
    calibration.monitor = Number(captureMonitorSelectEl.value) || 0;
    void loadCalibrationFrame();
  }
});
readBoardButton.addEventListener("click", () => {
  void readBoardFromCapture();
});
watchButton.addEventListener("click", () => {
  if (isWatching()) {
    stopWatch();
  } else {
    startWatch();
  }
});
capturePreviewButton.addEventListener("click", () => {
  if (capturePreviewTimer !== null) {
    stopCapturePreview();
  } else {
    startCapturePreview();
  }
});

loadSession();
loadSessionList();
refreshCaptureStatus();
