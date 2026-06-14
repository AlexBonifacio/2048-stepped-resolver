const SIZE = 4;

const params = new URLSearchParams(window.location.search);
const sessionName = params.get("session") || "try2";

const boardEl = document.querySelector("#board");
const statusEl = document.querySelector("#status");
const scoreEl = document.querySelector("#score");
const movesEl = document.querySelector("#moves");
const highestEl = document.querySelector("#highest");
const sessionNameEl = document.querySelector("#sessionName");
const newSessionNameEl = document.querySelector("#newSessionName");
const newSessionButton = document.querySelector("#newSessionButton");
const undoButton = document.querySelector("#undoButton");
const commitSpawnButton = document.querySelector("#commitSpawnButton");
const saveButton = document.querySelector("#saveButton");
const reloadButton = document.querySelector("#reloadButton");
const suggestionDirectionEl = document.querySelector("#suggestionDirection");
const suggestionMetaEl = document.querySelector("#suggestionMeta");
const suggestionRefreshButton = document.querySelector("#suggestionRefreshButton");
const suggestionRankingEl = document.querySelector("#suggestionRanking");
const contextScoreEl = document.querySelector("#contextScore");
const contextMovesEl = document.querySelector("#contextMoves");
const estimateMovesButton = document.querySelector("#estimateMovesButton");
const applyContextButton = document.querySelector("#applyContextButton");
const outcomeTargetEl = document.querySelector("#outcomeTarget");
const outcomeStatusEl = document.querySelector("#outcomeStatus");
const markOutcomeButton = document.querySelector("#markOutcomeButton");
const reopenOutcomeButton = document.querySelector("#reopenOutcomeButton");

let state = null;
let history = [];
let pendingSpawn = null;
let lastSavedAt = "";
let suggestionRequestId = 0;

const suggestionOptions = {
  solver: params.get("solver") || "hybrid",
  quality: params.get("quality") || "strong",
  modelSession: params.get("model_session") || "youtube_max01_VozVoz",
  modelStats: params.get("model_stats") || "session",
  timeout: params.get("timeout") || "45",
};
const defaultTargetRank = Math.max(1, Number(params.get("target")) || 13);

sessionNameEl.textContent = sessionName;

function emptySession() {
  return {
    moves: 0,
    score: 0,
    cells: Array(SIZE * SIZE).fill(0),
    map: Array.from({ length: SIZE }, () => Array(SIZE).fill(0)),
    observations: [],
    spawns: {},
    solved: [],
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
  syncOutcomeControls();
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
  const target = Math.max(1, Math.round(Number(outcomeTargetEl.value) || state.outcome?.target || defaultTargetRank));
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

function markOutcome(reason = "manual") {
  state.outcome = currentOutcome("ended", reason);
  syncOutcomeControls();
}

function reopenOutcome() {
  const target = Math.max(1, Math.round(Number(outcomeTargetEl.value) || state.outcome?.target || defaultTargetRank));
  state.outcome = { status: "in_progress", target };
  syncOutcomeControls();
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

function move(direction) {
  if (!state || pendingSpawn) {
    setStatus("Place d'abord la nouvelle case, ou vide-la avec clic droit.", "warn");
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
    setStatus("Aucun bloc ne bouge dans cette direction.", "warn");
    return;
  }

  pushHistory();
  recordSolvedMove(direction, beforeCells, result, context);
  state.cells = result.cells;
  state.score += result.gained;
  state.moves += 1;
  syncMap(state);
  pendingSpawn = { index: null, rank: 1, highestBeforeSpawn: beforeHighest };
  setStatus("Clique une case vide pour placer la nouvelle tuile. Clic gauche augmente, clic droit diminue jusqu'a vide.");
  setSuggestion("...", "Place la nouvelle tuile");
  render();
}

async function handleCellClick(index, event) {
  event.preventDefault();
  if (isInitialSetup()) {
    pushHistory();
    const delta = event.type === "contextmenu" ? -1 : 1;
    state.cells[index] = Math.max(0, state.cells[index] + delta);
    syncMap(state);
    render();
    await saveSession();
    await refreshSuggestion();
    return;
  }

  if (!pendingSpawn) {
    setStatus("Joue un coup avec les fleches avant de placer une nouvelle case.", "warn");
    return;
  }

  if (pendingSpawn.index === null) {
    if (state.cells[index] !== 0) {
      setStatus("Cette case existait deja, elle reste verrouillee.", "warn");
      return;
    }
    pendingSpawn.index = index;
    pendingSpawn.rank = event.type === "contextmenu" ? 0 : 1;
  } else if (pendingSpawn.index !== index) {
    setStatus("Seule la nouvelle case est editable jusqu'a la sauvegarde du coup.", "warn");
    return;
  } else {
    pendingSpawn.rank += event.type === "contextmenu" ? -1 : 1;
    pendingSpawn.rank = Math.max(0, pendingSpawn.rank);
  }

  render();
}

async function commitPendingSpawn({ save = true, refresh = true } = {}) {
  if (!pendingSpawn || pendingSpawn.index === null) {
    setStatus("Choisis une case vide avant de rejouer une fleche.", "warn");
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
    markOutcome("no_moves");
  }
  render();
  if (save) {
    await saveSession();
    if (refresh) {
      await refreshSuggestion();
    }
  }
  if (ended) {
    setStatus("Partie terminee. La session est sauvegardee.", "warn");
  }
  return true;
}

async function saveSession() {
  if (pendingSpawn) {
    setStatus("Valide d'abord la nouvelle case: elle sera sauvegardee avec le coup.", "warn");
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
    setStatus(payload.error || "Sauvegarde impossible.", "warn");
    return;
  }
  lastSavedAt = new Date().toLocaleTimeString();
  setStatus(`Sauvegarde OK dans ${payload.path} a ${lastSavedAt}.`);
}

function syncContextInputs() {
  if (!state) {
    contextScoreEl.value = "";
    contextMovesEl.value = "";
    return;
  }
  contextScoreEl.value = String(state.score);
  contextMovesEl.value = String(state.moves);
}

function syncOutcomeControls() {
  if (!state) {
    outcomeTargetEl.value = String(defaultTargetRank);
    outcomeStatusEl.textContent = "En cours";
    return;
  }
  state.outcome = normalizeOutcome(state.outcome);
  outcomeTargetEl.value = String(state.outcome.target);
  if (state.outcome.status === "ended") {
    const result = state.outcome.success ? "reussie" : "echouee";
    outcomeStatusEl.textContent = `Fin ${result}: max ${state.outcome.final_highest}/${state.outcome.target}, score ${state.outcome.final_score}`;
  } else {
    outcomeStatusEl.textContent = `En cours: cible ${state.outcome.target}`;
  }
}

async function loadSession() {
  const response = await fetch(`/api/session?name=${encodeURIComponent(sessionName)}`);
  const payload = await response.json();
  if (!response.ok) {
    state = emptySession();
    setStatus(payload.error || "Session introuvable.", "warn");
  } else {
    state = normalizeSession(payload.data);
    setStatus(`Session chargee depuis ${payload.path}.`);
  }
  history = [];
  pendingSpawn = null;
  syncContextInputs();
  syncOutcomeControls();
  render();
  await refreshSuggestion();
}

async function estimateMovesFromScore() {
  const score = Math.max(0, Number(contextScoreEl.value) || 0);
  if (score === 0) {
    contextMovesEl.value = "0";
    setStatus("Score nul: contexte remis au debut.");
    return;
  }

  estimateMovesButton.disabled = true;
  try {
    const response = await fetch(`/api/estimate-moves?score=${encodeURIComponent(score)}`);
    const payload = await response.json();
    if (!response.ok || !payload.ok) {
      setStatus(payload.error || "Estimation impossible.", "warn");
      return;
    }
    contextMovesEl.value = String(payload.moves);
    setStatus(`Estimation: ${payload.moves} coups pour score ${score} (${payload.confidence}, ${payload.references} refs).`);
  } catch (error) {
    setStatus("Serveur d'estimation indisponible.", "warn");
  } finally {
    estimateMovesButton.disabled = false;
  }
}

async function applyContext() {
  if (!state || pendingSpawn) {
    setStatus("Valide d'abord la tuile en cours avant de modifier le contexte.", "warn");
    return;
  }

  const score = Math.max(0, Math.round(Number(contextScoreEl.value) || 0));
  const moves = Math.max(0, Math.round(Number(contextMovesEl.value) || 0));
  pushHistory();
  state.score = score;
  state.moves = moves;
  if (state.outcome?.status === "ended") {
    state.outcome = currentOutcome("ended", state.outcome.reason || "manual");
  }
  syncContextInputs();
  syncOutcomeControls();
  render();
  await saveSession();
  await refreshSuggestion();
  setStatus(`Contexte applique: score ${score}, coups ${moves}.`);
}

async function saveManualOutcome() {
  if (!state || pendingSpawn) {
    setStatus("Valide d'abord la tuile en cours avant de marquer la fin.", "warn");
    return;
  }
  pushHistory();
  markOutcome("manual");
  render();
  await saveSession();
  setStatus(`Outcome sauvegarde: max ${state.outcome.final_highest}/${state.outcome.target}.`);
}

async function reopenManualOutcome() {
  if (!state) {
    return;
  }
  pushHistory();
  reopenOutcome();
  render();
  await saveSession();
  await refreshSuggestion();
  setStatus("Partie marquee comme en cours.");
}

async function createNewSession() {
  const rawName = newSessionNameEl.value.trim();
  if (!rawName) {
    setStatus("Donne un nom a la nouvelle session.", "warn");
    newSessionNameEl.focus();
    return;
  }

  const response = await fetch(`/api/new-session?name=${encodeURIComponent(rawName)}`, {
    method: "POST",
  });
  const payload = await response.json();
  if (!response.ok) {
    setStatus(payload.error || "Creation impossible.", "warn");
    return;
  }

  window.location.href = `/?session=${encodeURIComponent(rawName)}`;
}

function setStatus(text, mode = "") {
  statusEl.textContent = text;
  statusEl.className = `status ${mode}`.trim();
}

function setSuggestion(direction, meta, mode = "") {
  suggestionDirectionEl.textContent = direction;
  suggestionMetaEl.textContent = meta;
  suggestionMetaEl.className = mode;
}

function setSuggestionRanking(ranking = []) {
  suggestionRankingEl.replaceChildren();
  ranking.forEach((item, index) => {
    const entry = document.createElement("li");
    const direction = document.createElement("strong");
    const score = document.createElement("span");
    direction.textContent = `${index + 1}. ${item.direction}`;
    score.textContent = Number(item.score).toLocaleString("fr-FR", { maximumFractionDigits: 0 });
    entry.append(direction, score);
    suggestionRankingEl.append(entry);
  });
}

async function refreshSuggestion() {
  const requestId = ++suggestionRequestId;
  if (!state) {
    setSuggestion("...", "Session non chargee");
    setSuggestionRanking();
    return;
  }
  if (pendingSpawn) {
    setSuggestion("...", "Place la nouvelle tuile");
    setSuggestionRanking();
    return;
  }
  if (!canMove()) {
    setSuggestion("fin", "Aucun coup possible", "warn");
    setSuggestionRanking();
    return;
  }

  suggestionRefreshButton.disabled = true;
  setSuggestion("...", "Calcul IA en cours");
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
      setSuggestion("!", payload.error || "Calcul impossible", "warn");
      return;
    }
    const rollouts = Number(payload.rollouts) > 0 ? `, ${payload.rollouts} rollouts` : "";
    setSuggestion(
      payload.direction,
      `${payload.solver} ${payload.quality}, profondeur ${payload.depth}${rollouts}, modele ${payload.model_stats}, cible ${payload.target || defaultTargetRank}`,
    );
    setSuggestionRanking(payload.ranking || []);
  } catch (error) {
    if (requestId === suggestionRequestId) {
      setSuggestion("!", "Serveur IA indisponible", "warn");
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
  syncOutcomeControls();
  undoButton.disabled = history.length === 0;
  commitSpawnButton.disabled = !pendingSpawn;
  saveButton.disabled = Boolean(pendingSpawn);
  suggestionRefreshButton.disabled = Boolean(pendingSpawn);
  markOutcomeButton.disabled = Boolean(pendingSpawn);
  reopenOutcomeButton.disabled = state.outcome?.status !== "ended";

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
    setStatus("Retour effectue. Place la nouvelle case quand tu es pret.");
    setSuggestion("...", "Place la nouvelle tuile");
  }
});

commitSpawnButton.addEventListener("click", async () => {
  await commitPendingSpawn();
});
saveButton.addEventListener("click", async () => {
  await saveSession();
  await refreshSuggestion();
});
reloadButton.addEventListener("click", loadSession);
suggestionRefreshButton.addEventListener("click", refreshSuggestion);
estimateMovesButton.addEventListener("click", estimateMovesFromScore);
applyContextButton.addEventListener("click", applyContext);
markOutcomeButton.addEventListener("click", saveManualOutcome);
reopenOutcomeButton.addEventListener("click", reopenManualOutcome);
outcomeTargetEl.addEventListener("change", async () => {
  if (!state) {
    return;
  }
  const target = Math.max(1, Math.round(Number(outcomeTargetEl.value) || defaultTargetRank));
  state.outcome = state.outcome?.status === "ended"
    ? { ...currentOutcome("ended", state.outcome.reason || "manual"), target, success: highestRank() >= target }
    : { status: "in_progress", target };
  syncOutcomeControls();
  await saveSession();
});
contextScoreEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    estimateMovesFromScore();
  }
});
contextMovesEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    applyContext();
  }
});
newSessionButton.addEventListener("click", createNewSession);
newSessionNameEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    createNewSession();
  }
});

loadSession();
