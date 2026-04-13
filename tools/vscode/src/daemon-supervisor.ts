import * as fs from "fs";
import * as net from "net";
import {
  DaemonClient,
  DaemonNotification,
  StatusResult,
} from "./daemon-client";

export const EXPECTED_DAEMON_API_VERSION = 2;

export interface ExecResult {
  stdout: string;
  stderr: string;
}

export type ExecFroth = (args: string[]) => Promise<ExecResult>;

const daemonReadyTimeoutMs = 5000;
const daemonReadyPollMs = 100;

interface Hooks {
  onNotification: (n: DaemonNotification) => void;
  onClose: () => void;
}

export class DaemonModeConflictError extends Error {
  constructor(
    readonly currentMode: "serial" | "local",
    readonly requestedMode: "serial" | "local",
  ) {
    super(
      `daemon already running in ${currentMode} mode; stop it before switching to ${requestedMode}`,
    );
    this.name = "DaemonModeConflictError";
  }
}

export class DaemonSupervisor {
  private client: DaemonClient | null = null;
  private ownedPID: number | null = null;
  private activeSocketPath: string | null = null;
  private readonly hooks: Hooks;

  constructor(
    private readonly resolveSocketPath: () => string,
    private readonly execFroth: ExecFroth,
    hooks: Hooks,
  ) {
    this.hooks = hooks;
  }

  getClient(): DaemonClient | null {
    return this.client;
  }

  getSocketPath(): string {
    return this.currentSocketPath();
  }

  async deactivate(): Promise<void> {
    this.disposeClient();

    if (this.ownedPID !== null) {
      try {
        await this.execFroth(["daemon", "stop", "--pid", String(this.ownedPID)]);
      } catch {
        // Best effort.
      }
      this.ownedPID = null;
    }
    this.activeSocketPath = null;
  }

  async ensureMode(
    mode: "serial" | "local",
    localRuntimePath: string,
  ): Promise<StatusResult> {
    let status = await this.connectStatus();

    if (status && status.target !== mode) {
      if (this.ownedPID !== null && status.pid === this.ownedPID) {
        await this.stopRunningDaemon();
      } else {
        throw new DaemonModeConflictError(status.target, mode);
      }
      status = null;
    }

    if (!status) {
      status = await this.startAndConnect(mode, localRuntimePath);
    }

    return status;
  }

  async refreshStatus(): Promise<StatusResult | null> {
    if (!this.client) {
      return null;
    }

    try {
      const status = await this.client.status();
      this.validateStatus(status);
      return status;
    } catch (err) {
      if (isAPIMismatch(err)) {
        throw err;
      }
      this.disposeClient();
      return null;
    }
  }

  private async startAndConnect(
    mode: "serial" | "local",
    localRuntimePath: string,
  ): Promise<StatusResult> {
    const args = ["daemon", "start", "--background"];
    if (mode === "local") {
      args.push("--local");
      if (localRuntimePath.trim().length > 0) {
        args.push("--local-runtime", localRuntimePath.trim());
      }
    }

    const result = await this.execFroth(args);
    const start = parseDaemonStartOutput(result.stdout);
    if (!start) {
      throw new Error("daemon start did not return a pid");
    }
    this.ownedPID = start.alreadyRunning ? null : start.pid;

    const status = await this.connectRequiredWithRetry(daemonReadyTimeoutMs);
    if (status.target !== mode) {
      this.disposeClient();
      this.ownedPID = null;
      throw new DaemonModeConflictError(status.target, mode);
    }
    if (!start.alreadyRunning && status.pid !== start.pid) {
      this.ownedPID = status.pid;
    }
    return status;
  }

  private async stopRunningDaemon(): Promise<void> {
    try {
      await this.execFroth(["daemon", "stop"]);
    } catch {
      // Best effort.
    }
    await this.waitForDaemonStop(5000);
    this.disposeClient();
    this.ownedPID = null;
  }

  private async connectStatus(): Promise<StatusResult | null> {
    if (this.client) {
      try {
        const status = await this.client.status();
        this.validateStatus(status);
        return status;
      } catch (err) {
        if (isAPIMismatch(err)) {
          throw err;
        }
        this.disposeClient();
      }
    }

    try {
      return await this.connectRequired();
    } catch (err) {
      if (isAPIMismatch(err)) {
        throw err;
      }
      return null;
    }
  }

  private async connectRequired(): Promise<StatusResult> {
    const socketPath = this.currentSocketPath();
    const client = new DaemonClient();
    client.onNotification(this.hooks.onNotification);
    client.onClose(this.hooks.onClose);

    try {
      await client.connect(socketPath);
      const status = await client.status();
      this.validateStatus(status);
      this.disposeClient();
      this.client = client;
      this.activeSocketPath = socketPath;
      return status;
    } catch (err) {
      client.dispose();
      throw err;
    }
  }

  private async connectRequiredWithRetry(timeoutMs: number): Promise<StatusResult> {
    const deadline = Date.now() + timeoutMs;
    let lastError: unknown;

    for (;;) {
      try {
        return await this.connectRequired();
      } catch (err) {
        if (isAPIMismatch(err)) {
          throw err;
        }
        lastError = err;
      }

      if (Date.now() >= deadline) {
        if (lastError instanceof Error) {
          throw lastError;
        }
        throw new Error("daemon did not become ready");
      }

      await sleep(daemonReadyPollMs);
    }
  }

  private validateStatus(status: StatusResult): void {
    if (status.api_version !== EXPECTED_DAEMON_API_VERSION) {
      throw new Error(
        `daemon api mismatch: expected ${EXPECTED_DAEMON_API_VERSION}, got ${status.api_version}`,
      );
    }
  }

  private async waitForDaemonStop(timeoutMs: number): Promise<void> {
    const deadline = Date.now() + timeoutMs;

    for (;;) {
      if (Date.now() >= deadline) {
        throw new Error("daemon did not stop");
      }

      const socketPath = this.currentSocketPath();
      if (!fs.existsSync(socketPath)) {
        return;
      }

      const ready = await this.socketAlive(socketPath);
      if (!ready) {
        return;
      }

      await sleep(100);
    }
  }

  private async socketAlive(socketPath: string): Promise<boolean> {
    return new Promise<boolean>((resolve) => {
      const sock = net.createConnection(socketPath);

      let settled = false;

      sock.on("connect", () => {
        settled = true;
        sock.destroy();
        resolve(true);
      });

      sock.on("error", () => {
        if (!settled) {
          settled = true;
          resolve(false);
        }
      });
    });
  }

  private currentSocketPath(): string {
    return this.activeSocketPath ?? this.resolveSocketPath();
  }

  private disposeClient(): void {
    if (!this.client) {
      return;
    }
    this.client.dispose();
    this.client = null;
    this.activeSocketPath = null;
  }
}

interface DaemonStartOutput {
  pid: number;
  alreadyRunning: boolean;
}

function parseDaemonStartOutput(stdout: string): DaemonStartOutput | null {
  const lines = stdout
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);

  for (let i = lines.length - 1; i >= 0; i--) {
    const line = lines[i];

    if (/^\d+$/.test(line)) {
      return {
        pid: Number.parseInt(line, 10),
        alreadyRunning: false,
      };
    }

    const runningMatch = /^daemon already running \(pid (\d+)\)$/.exec(line);
    if (runningMatch) {
      return {
        pid: Number.parseInt(runningMatch[1], 10),
        alreadyRunning: true,
      };
    }
  }

  return null;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function isAPIMismatch(err: unknown): boolean {
  return err instanceof Error && err.message.startsWith("daemon api mismatch:");
}
