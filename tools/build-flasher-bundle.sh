#!/usr/bin/env bash
set -euo pipefail

# Build every official board and write one browser-flasher bundle. Targets own
# their artifact paths: ESP-IDF supplies flasher_args.json, while UF2 targets
# declare ARTIFACT_UF2 through make print-config.
#
# Prereq: ESP-IDF plus each non-ESP target's pinned toolchain.
# Usage: tools/build-flasher-bundle.sh <dest-firmware-dir>
#   e.g. tools/build-flasher-bundle.sh ~/Developer/frothy-site/static/test/flash/firmware

dest="${1:?usage: build-flasher-bundle.sh <dest-firmware-dir>}"
here="$(cd "$(dirname "$0")/.." && pwd)"
version="$("$here/tools/release-name.sh")"

node - "$here" "$dest" "$version" <<'NODE'
const childProcess = require("child_process");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const [root, destinationArg, version] = process.argv.slice(2);
const boardsDir = path.join(root, "boards");
const destination = path.resolve(destinationArg);
const generatedSegment = /^.+-.+-0x[0-9a-fA-F]+\.bin$/;
const generatedUF2 = /^[a-z0-9_]+-[a-z0-9_]+\.uf2$/;
const bundleIdPattern = /^[a-z0-9_]+$/;
const outputFiles = new Set();
const builds = [];
const rp2040FamilyId = 0xe48bff56;
const rp2040FlashStart = 0x10000000;
const rp2040FlashEnd = 0x11000000;

function makeConfig(boardId, profile) {
  const result = childProcess.spawnSync(
    "make",
    ["--no-print-directory", "-s", "-C", root, `BOARD=${boardId}`, `PROFILE=${profile}`, "print-config"],
    { encoding: "utf8" },
  );
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${boardId}: could not read build configuration`);
  return Object.fromEntries(result.stdout.trimEnd().split("\n").map((line) => {
    const separator = line.indexOf("=");
    if (separator < 1) throw new Error(`${boardId}: invalid print-config line: ${line}`);
    return [line.slice(0, separator), line.slice(separator + 1)];
  }));
}

function validateRP2040UF2(data, boardId) {
  if (data.length === 0 || data.length % 512 !== 0) {
    throw new Error(`${boardId}: UF2 size is not a nonzero multiple of 512`);
  }

  let blockCount = null;
  const numbers = new Set();
  const ranges = [];
  for (let offset = 0; offset < data.length; offset += 512) {
    const block = data.subarray(offset, offset + 512);
    const index = offset / 512;
    if (block.readUInt32LE(0) !== 0x0a324655 ||
        block.readUInt32LE(4) !== 0x9e5d5157 ||
        block.readUInt32LE(508) !== 0x0ab16f30) {
      throw new Error(`${boardId}: invalid UF2 magic at block ${index}`);
    }

    const flags = block.readUInt32LE(8);
    const address = block.readUInt32LE(12);
    const payloadSize = block.readUInt32LE(16);
    const blockNumber = block.readUInt32LE(20);
    const declaredCount = block.readUInt32LE(24);
    const familyId = block.readUInt32LE(28);
    if ((flags & 0x00000001) !== 0 || (flags & 0x00002000) === 0 ||
        familyId !== rp2040FamilyId) {
      throw new Error(`${boardId}: block ${index} is not RP2040 flash data`);
    }
    if (payloadSize !== 256 || address % 256 !== 0 ||
        address < rp2040FlashStart || address + payloadSize > rp2040FlashEnd) {
      throw new Error(`${boardId}: invalid UF2 target at block ${index}`);
    }
    if (blockCount === null) blockCount = declaredCount;
    if (declaredCount !== blockCount || blockNumber >= declaredCount ||
        numbers.has(blockNumber)) {
      throw new Error(`${boardId}: invalid UF2 numbering at block ${index}`);
    }
    numbers.add(blockNumber);
    ranges.push({ start: address, end: address + payloadSize });
  }

  if (numbers.size !== blockCount) {
    throw new Error(`${boardId}: UF2 block count is incomplete`);
  }
  ranges.sort((a, b) => a.start - b.start);
  for (let index = 1; index < ranges.length; index++) {
    if (ranges[index].start !== ranges[index - 1].end) {
      throw new Error(`${boardId}: UF2 flash range is not contiguous`);
    }
  }
}

for (const entry of fs.readdirSync(boardsDir, { withFileTypes: true })
  .filter((candidate) => candidate.isDirectory())
  .sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0)) {
  const boardId = entry.name;
  const boardFile = path.join(boardsDir, boardId, "board.json");
  if (!fs.existsSync(boardFile)) continue;

  const board = JSON.parse(fs.readFileSync(boardFile, "utf8"));
  if (board.target === "host") continue;
  if (!bundleIdPattern.test(boardId)) throw new Error(`invalid board id: ${boardId}`);
  if (typeof board.name !== "string" || !board.name) {
    throw new Error(`${boardId}: board name missing`);
  }
  if (typeof board.profile !== "string" || !board.profile) {
    throw new Error(`${boardId}: board profile missing`);
  }
  if (typeof board.chip !== "string" || !/^[a-z0-9]+$/.test(board.chip)) {
    throw new Error(`${boardId}: board chip missing or invalid`);
  }
  if (!bundleIdPattern.test(board.profile)) {
    throw new Error(`${boardId}: invalid board profile ${board.profile}`);
  }

  const result = childProcess.spawnSync(
    "make",
    ["-C", root, `BOARD=${boardId}`, `PROFILE=${board.profile}`, "artifacts"],
    { stdio: "inherit" },
  );
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${boardId}: firmware build failed`);

  const buildRoot = path.join(root, "build", boardId);
  const argsFile = path.join(buildRoot, "flasher_args.json");
  const config = makeConfig(boardId, board.profile);
  const uf2Source = config.ARTIFACT_UF2
    ? path.resolve(root, config.ARTIFACT_UF2)
    : "";
  if (fs.existsSync(argsFile) === Boolean(uf2Source)) {
    throw new Error(`${boardId}: expected exactly one segmented or UF2 artifact`);
  }

  const prefix = `${boardId}-${board.profile}-`;
  if (uf2Source) {
    if (typeof board.bootsel !== "string" || !board.bootsel) {
      throw new Error(`${boardId}: UF2 board is missing BOOTSEL instructions`);
    }
    const sourceRelative = path.relative(buildRoot, uf2Source);
    if (sourceRelative.startsWith(`..${path.sep}`) || path.isAbsolute(sourceRelative) ||
        !fs.existsSync(uf2Source) || !fs.statSync(uf2Source).isFile()) {
      throw new Error(`${boardId}: declared UF2 artifact is missing or outside its build directory`);
    }
    const data = fs.readFileSync(uf2Source);
    validateRP2040UF2(data, boardId);
    const file = `${boardId}-${board.profile}.uf2`;
    if (outputFiles.has(file)) throw new Error(`duplicate output file: ${file}`);
    outputFiles.add(file);
    builds.push({
      boardId,
      board,
      uf2: {
        file,
        md5: crypto.createHash("md5").update(data).digest("hex"),
        source: uf2Source,
      },
    });
    continue;
  }

  const args = JSON.parse(fs.readFileSync(argsFile, "utf8"));
  if (!args.flash_files || typeof args.flash_files !== "object" || Array.isArray(args.flash_files)) {
    throw new Error(`${boardId}: flasher_args.json has no flash_files object`);
  }
  const addresses = new Set();
  const segments = Object.entries(args.flash_files).map(([encodedAddress, relativeFile]) => {
    const address = Number(encodedAddress);
    if (!Number.isSafeInteger(address) || address < 0) {
      throw new Error(`${boardId}: invalid flash address ${encodedAddress}`);
    }
    if (addresses.has(address)) {
      throw new Error(`${boardId}: duplicate flash address ${encodedAddress}`);
    }
    addresses.add(address);
    if (typeof relativeFile !== "string" || !relativeFile) {
      throw new Error(`${boardId}: invalid flash file at ${encodedAddress}`);
    }

    const source = path.resolve(buildRoot, relativeFile);
    const sourceRelative = path.relative(buildRoot, source);
    if (sourceRelative.startsWith(`..${path.sep}`) || path.isAbsolute(sourceRelative)) {
      throw new Error(`${boardId}: flash file escapes build directory: ${relativeFile}`);
    }
    if (!fs.statSync(source).isFile()) {
      throw new Error(`${boardId}: flash file is missing: ${relativeFile}`);
    }

    const file = `${prefix}0x${address.toString(16).padStart(8, "0")}.bin`;
    if (outputFiles.has(file)) throw new Error(`duplicate output file: ${file}`);
    outputFiles.add(file);
    const md5 = crypto.createHash("md5").update(fs.readFileSync(source)).digest("hex");
    return { address, file, md5, source };
  }).sort((a, b) => a.address - b.address);
  if (segments.length === 0) {
    throw new Error(`${boardId}: flasher_args.json lists no flash files`);
  }
  builds.push({ boardId, board, segments });
}

if (builds.length === 0) throw new Error("no official firmware boards found");

const manifest = builds.map(({ boardId, board, segments, uf2 }) => {
  const row = {
    board: boardId,
    chip: board.chip,
    profile: board.profile,
    label: board.name,
    version,
  };
  if (uf2) {
    row.bootsel = board.bootsel;
    row.uf2 = { file: uf2.file, md5: uf2.md5 };
  } else {
    row.segments = segments.map(({ address, file, md5 }) => ({ address, file, md5 }));
  }
  return row;
});

const parent = path.dirname(destination);
const destinationName = path.basename(destination);
fs.mkdirSync(parent, { recursive: true });
const stage = fs.mkdtempSync(path.join(parent, `.${destinationName}.stage-`));
let backup = "";

try {
  if (fs.existsSync(destination)) {
    if (!fs.lstatSync(destination).isDirectory()) {
      throw new Error(`destination is not a directory: ${destination}`);
    }
    const currentLegacyImages = new Set(
      builds.map(({ boardId, board }) => `${boardId}-${board.profile}.bin`),
    );
    for (const entry of fs.readdirSync(destination, { withFileTypes: true })) {
      if (entry.name === "manifest.json" || generatedSegment.test(entry.name) ||
          generatedUF2.test(entry.name) ||
          currentLegacyImages.has(entry.name)) {
        continue;
      }
      fs.cpSync(path.join(destination, entry.name), path.join(stage, entry.name), {
        recursive: true,
      });
    }
  }

  for (const { segments, uf2 } of builds) {
    for (const segment of segments ?? []) {
      fs.copyFileSync(segment.source, path.join(stage, segment.file));
    }
    if (uf2) fs.copyFileSync(uf2.source, path.join(stage, uf2.file));
  }
  fs.writeFileSync(
    path.join(stage, "manifest.json"),
    JSON.stringify(manifest, null, 2) + "\n",
  );

  if (fs.existsSync(destination)) {
    backup = path.join(parent, `.${destinationName}.backup-${process.pid}-${Date.now()}`);
    fs.renameSync(destination, backup);
  }
  fs.renameSync(stage, destination);
  if (backup) fs.rmSync(backup, { recursive: true, force: true });
} catch (error) {
  if (backup && fs.existsSync(backup) && !fs.existsSync(destination)) {
    fs.renameSync(backup, destination);
  }
  if (fs.existsSync(stage)) fs.rmSync(stage, { recursive: true, force: true });
  throw error;
}

const fileCount = builds.reduce(
  (count, build) => count + (build.segments?.length ?? 1),
  0,
);
console.log(`built ${fileCount} firmware files for ${builds.length} boards @ ${version} -> ${destination}`);
NODE
