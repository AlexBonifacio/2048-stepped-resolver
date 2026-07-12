// Thin JS API around the WASM build of the 2048-ranks solver.
//
// The C++ solver is unchanged: it still reads data/sessions/<name>.json and
// friends through relative paths. Each call gets a fresh module instance
// with those files staged in the in-memory filesystem (MEMFS), then the
// JSON printed on stdout is returned, exactly like web/server.py does with
// the native binary.

import createSolverModule from "./2048-ranks.mjs";

const WORK_DIR = "/work";

export async function runSolver(args, files = {}) {
  const stdout = [];
  const stderr = [];
  const module = await createSolverModule({
    print: (line) => stdout.push(line),
    printErr: (line) => stderr.push(line),
  });

  module.FS.mkdirTree(`${WORK_DIR}/data/sessions`);
  module.FS.mkdirTree(`${WORK_DIR}/data/simulation_reports`);
  for (const [path, content] of Object.entries(files)) {
    const full = `${WORK_DIR}/${path}`;
    module.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
    module.FS.writeFile(full, content);
  }
  module.FS.chdir(WORK_DIR);

  const code = module.callMain(args);
  return {
    code,
    stdout: stdout.join("\n"),
    stderr: stderr.join("\n"),
    readFile: (path) => module.FS.readFile(`${WORK_DIR}/${path}`, { encoding: "utf8" }),
  };
}

export async function suggest({
  session,
  sessionName = "web",
  options = {},
  observedSpawns = null,
  extraSessions = {},
}) {
  const files = {
    [`data/sessions/${sessionName}.json`]: JSON.stringify(session),
  };
  if (observedSpawns) {
    files["data/observed_spawns.json"] = JSON.stringify(observedSpawns);
  }
  for (const [name, data] of Object.entries(extraSessions)) {
    files[`data/sessions/${name}.json`] = JSON.stringify(data);
  }

  const args = [
    "--suggest-only",
    "--session", sessionName,
    "--solver", options.solver || "hybrid",
    "--quality", options.quality || "godlike",
    "--model-stats", options.modelStats || "sessions",
    "--target", String(options.target || 12),
  ];
  if (options.modelSession) {
    args.push("--model-session", options.modelSession);
  }

  const result = await runSolver(args, files);
  try {
    return JSON.parse(result.stdout);
  } catch (error) {
    return {
      ok: false,
      error: "Invalid solver output.",
      stdout: result.stdout.slice(-500),
      stderr: result.stderr.slice(-500),
    };
  }
}
