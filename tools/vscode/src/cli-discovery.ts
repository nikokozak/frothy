import * as fs from "fs";
import * as path from "path";

export function cliCandidates(cwd: string): string[] {
  // Prefer the installed Frothy CLI command (`froth`) first, then fall back
  // to repo-local checkout builds.
  return dedupe([
    "froth",
    "/opt/homebrew/bin/froth",
    "/usr/local/bin/froth",
    "froth-cli",
    ...repoLocalCandidates(cwd),
  ]);
}

export function resolveCliCandidate(
  candidate: string,
  cwd: string,
  pathEnv: string = process.env.PATH ?? "",
): string | null {
  if (path.isAbsolute(candidate)) {
    return fs.existsSync(candidate) ? candidate : null;
  }

  if (candidate.includes(path.sep)) {
    const absolute = path.resolve(cwd, candidate);
    return fs.existsSync(absolute) ? absolute : null;
  }

  for (const dir of pathEnv.split(path.delimiter)) {
    if (!dir) {
      continue;
    }
    const fullPath = path.join(dir, candidate);
    if (fs.existsSync(fullPath)) {
      return fullPath;
    }
  }

  return null;
}

function repoLocalCandidates(cwd: string): string[] {
  const candidates: string[] = [];
  let current = path.resolve(cwd);

  for (;;) {
    // Repo-local checkouts may expose either the default `froth-cli` build
    // output or a manually named `froth` sibling without changing lookup
    // order.
    candidates.push(path.join(current, "tools", "cli", "froth-cli"));
    candidates.push(path.join(current, "tools", "cli", "froth"));

    const parent = path.dirname(current);
    if (parent === current) {
      return candidates;
    }
    current = parent;
  }
}

function dedupe(values: string[]): string[] {
  return [...new Set(values)];
}
