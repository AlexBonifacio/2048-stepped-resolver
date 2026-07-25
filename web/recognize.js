// Board recognition from raw pixels, in the browser.
//
// Direct port of web/recognize.py (kept in lockstep — see its docstring for
// the algorithm): each game tile shows a beige digit badge in its bottom
// right corner whose value is directly the rank. Warm beige pixels are
// thresholded adaptively, segmented into digits, and matched against the
// templates of web/digit_templates.json.
//
// Pixels are addressed through a flat RGBA buffer ({data, width, height}),
// which is exactly what canvas getImageData returns.

const BOARD_SIZE = 4;
const TEMPLATE_WIDTH = 12;
const TEMPLATE_HEIGHT = 20;

const WINDOW_LEFT = 0.40;
const WINDOW_TOP = 0.45;

const BRIGHTNESS_RATIO = 0.62;
const BRIGHTNESS_RATIOS = [0.85, 0.75, BRIGHTNESS_RATIO];
const BRIGHTNESS_FLOOR = 110;

const MIN_COMPONENT_PIXELS = 12;
const MIN_MATCH_SCORE = 0.72;
const GOOD_MATCH_SCORE = 0.8;
const MAX_RANK = 16;

function warmShape(r, g, b) {
  if (r < g || g + 6 < b) {
    return false;
  }
  const spread = r ? (r - b) / r : 0;
  const tilt = r ? (r - g) / r : 0;
  return spread >= 0.04 && spread <= 0.36 && tilt <= 0.16;
}

function extractDigitSegments(image, x0, y0, x1, y1, ratio = BRIGHTNESS_RATIO) {
  const { data, width } = image;
  const wx0 = x0 + Math.trunc((x1 - x0) * WINDOW_LEFT);
  const wy0 = y0 + Math.trunc((y1 - y0) * WINDOW_TOP);
  const wx1 = x1;
  const wy1 = y1;

  let brightest = 0;
  for (let y = wy0; y < wy1; y += 1) {
    const row = y * width * 4;
    for (let x = wx0; x < wx1; x += 1) {
      const offset = row + x * 4;
      const r = data[offset];
      if (r > brightest && warmShape(r, data[offset + 1], data[offset + 2])) {
        brightest = r;
      }
    }
  }
  const floor = Math.max(BRIGHTNESS_FLOOR, Math.trunc(brightest * ratio));
  if (brightest < BRIGHTNESS_FLOOR) {
    return [];
  }

  const windowWidth = wx1 - wx0;
  const windowHeight = wy1 - wy0;
  const mask = new Uint8Array(windowWidth * windowHeight);
  for (let y = 0; y < windowHeight; y += 1) {
    const row = (wy0 + y) * width * 4;
    for (let x = 0; x < windowWidth; x += 1) {
      const offset = row + (wx0 + x) * 4;
      const r = data[offset];
      if (r >= floor && warmShape(r, data[offset + 1], data[offset + 2])) {
        mask[y * windowWidth + x] = 1;
      }
    }
  }

  let components = connectedComponents(mask, windowWidth, windowHeight)
    .filter((component) => component.length >= MIN_COMPONENT_PIXELS);
  if (components.length === 0) {
    return [];
  }

  const tallest = Math.max(...components.map(componentHeight));
  if (tallest < windowHeight * 0.12) {
    return [];
  }
  let kept = components.filter((component) => componentHeight(component) >= tallest * 0.5);

  // The digit badge always sits at the bottom of the corner window, while
  // icon parts (box faces, glints) float higher. Anchor on the bottom-most
  // tall component and keep only components sharing its vertical band.
  const reference = kept.reduce((best, component) =>
    (componentBottom(component) > componentBottom(best) ? component : best));
  const refTop = Math.min(...reference.map((p) => p[1]));
  const refBottom = componentBottom(reference);
  const margin = (refBottom - refTop) * 0.6;
  kept = kept.filter((component) => {
    const top = Math.min(...component.map((p) => p[1]));
    const bottom = componentBottom(component);
    return top >= refTop - margin && bottom <= refBottom + margin;
  });

  return mergeOverlappingColumns(kept);
}

function connectedComponents(mask, width, height) {
  const seen = new Uint8Array(width * height);
  const components = [];
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const start = y * width + x;
      if (!mask[start] || seen[start]) {
        continue;
      }
      const stack = [[x, y]];
      seen[start] = 1;
      const pixels = [];
      while (stack.length > 0) {
        const [cx, cy] = stack.pop();
        pixels.push([cx, cy]);
        for (let dy = -1; dy <= 1; dy += 1) {
          for (let dx = -1; dx <= 1; dx += 1) {
            const nx = cx + dx;
            const ny = cy + dy;
            const index = ny * width + nx;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height && mask[index] && !seen[index]) {
              seen[index] = 1;
              stack.push([nx, ny]);
            }
          }
        }
      }
      components.push(pixels);
    }
  }
  return components;
}

function componentHeight(component) {
  let min = Infinity;
  let max = -Infinity;
  for (const [, y] of component) {
    if (y < min) min = y;
    if (y > max) max = y;
  }
  return max - min + 1;
}

function componentBottom(component) {
  let max = -Infinity;
  for (const [, y] of component) {
    if (y > max) max = y;
  }
  return max;
}

function mergeOverlappingColumns(components) {
  const intervals = components.map((component) => {
    const xs = component.map((p) => p[0]);
    return [Math.min(...xs), Math.max(...xs), component.slice()];
  });
  intervals.sort((a, b) => a[0] - b[0]);

  const merged = [];
  for (const [left, right, pixels] of intervals) {
    const last = merged[merged.length - 1];
    if (last && left <= last[1] + 1) {
      last[1] = Math.max(last[1], right);
      last[2].push(...pixels);
    } else {
      merged.push([left, right, pixels]);
    }
  }
  return merged.map((entry) => entry[2]);
}

function normalizeSegment(segment) {
  const xs = segment.map((p) => p[0]);
  const ys = segment.map((p) => p[1]);
  const left = Math.min(...xs);
  const top = Math.min(...ys);
  const boxWidth = Math.max(...xs) - left + 1;
  const boxHeight = Math.max(...ys) - top + 1;
  const filled = new Set(segment.map(([x, y]) => `${x},${y}`));

  const rows = [];
  for (let ty = 0; ty < TEMPLATE_HEIGHT; ty += 1) {
    const y0 = top + Math.trunc((ty * boxHeight) / TEMPLATE_HEIGHT);
    const y1 = Math.max(y0 + 1, top + Math.trunc(((ty + 1) * boxHeight) / TEMPLATE_HEIGHT));
    let row = "";
    for (let tx = 0; tx < TEMPLATE_WIDTH; tx += 1) {
      const x0 = left + Math.trunc((tx * boxWidth) / TEMPLATE_WIDTH);
      const x1 = Math.max(x0 + 1, left + Math.trunc(((tx + 1) * boxWidth) / TEMPLATE_WIDTH));
      let hit = false;
      for (let y = y0; y < y1 && !hit; y += 1) {
        for (let x = x0; x < x1 && !hit; x += 1) {
          hit = filled.has(`${x},${y}`);
        }
      }
      row += hit ? "1" : "0";
    }
    rows.push(row);
  }
  return rows;
}

function matchDigit(bitmap, templates) {
  let bestDigit = null;
  let bestScore = -1;
  const total = TEMPLATE_WIDTH * TEMPLATE_HEIGHT;
  for (const [digit, candidates] of Object.entries(templates)) {
    for (const candidate of candidates) {
      let same = 0;
      for (let row = 0; row < TEMPLATE_HEIGHT; row += 1) {
        for (let column = 0; column < TEMPLATE_WIDTH; column += 1) {
          if (bitmap[row][column] === candidate[row][column]) {
            same += 1;
          }
        }
      }
      const score = same / total;
      if (score > bestScore) {
        bestDigit = digit;
        bestScore = score;
      }
    }
  }
  return [bestDigit, bestScore];
}

function readCell(image, box, templates) {
  const [x0, y0, x1, y1] = box;
  let foundAnything = false;
  let bestRank = null;
  let bestScore = 0;
  for (const ratio of BRIGHTNESS_RATIOS) {
    const segments = extractDigitSegments(image, x0, y0, x1, y1, ratio);
    if (segments.length === 0) {
      continue;
    }
    foundAnything = true;
    let digits = "";
    let worst = 1;
    let unreadable = false;
    for (const segment of segments) {
      const [digit, score] = matchDigit(normalizeSegment(segment), templates);
      worst = Math.min(worst, score);
      if (digit === null) {
        unreadable = true;
        break;
      }
      digits += digit;
    }
    if (unreadable) {
      continue;
    }
    const rank = Number(digits);
    if (!Number.isInteger(rank) || rank < 1 || rank > MAX_RANK) {
      continue;
    }
    if (worst >= GOOD_MATCH_SCORE) {
      return [rank, worst];
    }
    if (worst > bestScore) {
      bestRank = rank;
      bestScore = worst;
    }
  }
  if (bestRank !== null && bestScore >= MIN_MATCH_SCORE) {
    return [bestRank, bestScore];
  }
  if (foundAnything) {
    return [null, bestScore];
  }
  return [0, 1];
}

function recognizeBoard(image, templates) {
  const cells = [];
  const confidences = [];
  const { width, height } = image;
  for (let row = 0; row < BOARD_SIZE; row += 1) {
    for (let column = 0; column < BOARD_SIZE; column += 1) {
      const box = [
        Math.trunc((column * width) / BOARD_SIZE),
        Math.trunc((row * height) / BOARD_SIZE),
        Math.trunc(((column + 1) * width) / BOARD_SIZE),
        Math.trunc(((row + 1) * height) / BOARD_SIZE),
      ];
      const [rank, confidence] = readCell(image, box, templates);
      cells.push(rank);
      confidences.push(Math.round(confidence * 10000) / 10000);
    }
  }
  return { cells, confidences };
}
