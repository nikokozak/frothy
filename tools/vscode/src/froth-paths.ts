import * as os from "os";
import * as path from "path";

export function resolveFrothHome(
  env: NodeJS.ProcessEnv = process.env,
  homeDir: string | null = safeHomeDir(),
  cwd: string = process.cwd(),
): string {
  const configured = env.FROTH_HOME;
  if (configured && configured.length > 0) {
    return path.isAbsolute(configured) ? configured : path.resolve(cwd, configured);
  }

  if (homeDir && homeDir.length > 0) {
    return path.join(homeDir, ".froth");
  }

  return path.resolve(cwd, ".froth");
}

export function resolveDaemonSocketPath(
  env: NodeJS.ProcessEnv = process.env,
  homeDir: string | null = safeHomeDir(),
  cwd: string = process.cwd(),
): string {
  return path.join(resolveFrothHome(env, homeDir, cwd), "daemon.sock");
}

function safeHomeDir(): string | null {
  try {
    const homeDir = os.homedir();
    return homeDir.length > 0 ? homeDir : null;
  } catch {
    return null;
  }
}
