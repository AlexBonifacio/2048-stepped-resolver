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
const saveButton = document.querySelector("#saveButton");
const reloadButton = document.querySelector("#reloadButton");

let state = null;
let history = [];
let pendingSpawn = null;
let lastSavedAt = "";

sessionNameEl.textContent = sessionName;

function emptySession() {
  return {
    moves: 0,
    score: 0,
    cells: Array(SIZE * SIZE).fill(0),
    map: Array.from({ length: SIZE }, () => Array(SIZE).fill(0)),
    observations: [],
    spawns: {},
  };
}

function normalizeSession(data) {
  const next = { ...emptySession(), ...data };
  if (Array.isArray(next.cells) && next.cells.length >= SIZE * SIZE) {
    next.cells = next.cells.slice(0, SIZE * SIZE).map((value) => Math.max(0, Number(value) || 0));
  } else if (Array.isArray(next.map)) {
    next.cells = next.map.flat().slice(0, SIZE * SIZE).map((value) => Math.max(0, Number(value) || 0));
  }
  next.moves = Math.max(0, Number(next.moves) || 0);
  next.score = Math.max(0, Number(next.score) || 0);
  next.observations = Array.isArray(next.observations) ? next.observations : [];
  next.spawns = next.spawns && typeof next.spawns === "object" ? next.spawns : {};
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

function move(direction) {
  if (!state || pendingSpawn) {
    setStatus("Place d'abord la nouvelle case, ou vide-la avec clic droit.", "warn");
    return;
  }

  const beforeHighest = highestRank();
  const result = applyMove(direction);
  if (!result.changed) {
    setStatus("Aucun bloc ne bouge dans cette direction.", "warn");
    return;
  }

  pushHistory();
  state.cells = result.cells;
  state.score += result.gained;
  state.moves += 1;
  syncMap(state);
  pendingSpawn = { index: null, rank: 1, highestBeforeSpawn: beforeHighest };
  setStatus("Clique une case vide pour placer la nouvelle tuile. Clic gauche augmente, clic droit diminue jusqu'a vide.");
  render();
}

function handleCellClick(index, event) {
  event.preventDefault();
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

async function commitPendingSpawn({ save = true } = {}) {
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
  render();
  if (save) {
    await saveSession();
  }
  if (!pendingSpawn && !canMove()) {
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
  render();
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
  saveButton.disabled = Boolean(pendingSpawn);

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
    button.addEventListener("click", (event) => handleCellClick(index, event));
    button.addEventListener("contextmenu", (event) => handleCellClick(index, event));
    boardEl.append(button);
  });
}

document.addEventListener("keydown", async (event) => {
  const direction = {
    ArrowLeft: "left",
    ArrowRight: "right",
    ArrowUp: "up",
    ArrowDown: "down",
  }[event.key];

  if (direction) {
    event.preventDefault();
    if (pendingSpawn) {
      const committed = await commitPendingSpawn();
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
  } else {
    setStatus("Retour effectue. Place la nouvelle case quand tu es pret.");
  }
});

saveButton.addEventListener("click", saveSession);
reloadButton.addEventListener("click", loadSession);
newSessionButton.addEventListener("click", createNewSession);
newSessionNameEl.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    event.preventDefault();
    createNewSession();
  }
});

loadSession();
