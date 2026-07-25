// Web Worker wrapper: runs the WASM solver off the main thread so godlike
// suggestions (about a second of CPU) never freeze the page.

import { suggest } from "./solver-api.mjs";

self.onmessage = async (event) => {
  const { id, payload } = event.data;
  try {
    const result = await suggest(payload);
    self.postMessage({ id, result });
  } catch (error) {
    self.postMessage({
      id,
      result: { ok: false, error: String((error && error.message) || error) },
    });
  }
};
