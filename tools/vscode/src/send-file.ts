import { execFile } from "child_process";
import { promisify } from "util";

const execFileAsync = promisify(execFile);

type ExecFileResult = {
  stdout: string;
  stderr: string;
};

type ExecFileLike = (
  file: string,
  args: readonly string[],
  options: { cwd: string },
) => Promise<ExecFileResult>;

export async function resolveSendSourceCommand(
  cliPath: string,
  filePath: string,
  cwd: string,
  execCli: ExecFileLike = execFileAsync,
): Promise<ExecFileResult & { source: string }> {
  try {
    const result = await execCli(
      cliPath,
      ["tooling", "resolve-source", filePath],
      { cwd },
    );
    return {
      ...result,
      source: result.stdout,
    };
  } catch (err: unknown) {
    const childErr = err as NodeJS.ErrnoException & {
      stdout?: string;
      stderr?: string;
    };

    if (childErr.code === "ENOENT") {
      throw new Error(
        "Frothy CLI not found. Install `froth` and ensure it is on PATH, or set frothy.cliPath.",
      );
    }
    if (isMissingResolveSourceSupport(childErr)) {
      throw new Error(
        "The detected froth CLI is too old for Frothy Send File. Upgrade `froth` to a build that supports `froth tooling resolve-source`.",
      );
    }
    throw err;
  }
}

export function isMissingResolveSourceSupport(err: {
  stdout?: string;
  stderr?: string;
  message?: string;
}): boolean {
  const combined = `${err.stdout ?? ""}\n${err.stderr ?? ""}\n${err.message ?? ""}`;
  return (
    combined.includes("unknown command: tooling") ||
    combined.includes("unknown tooling command") ||
    combined.includes("unknown command \"tooling\"")
  );
}
