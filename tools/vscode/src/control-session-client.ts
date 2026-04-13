import { EventEmitter } from "events";
import { ChildProcessWithoutNullStreams, spawn } from "child_process";

export interface ConnectCandidate {
  port: string;
  board?: string;
  version?: string;
}

export interface DeviceInfo {
  port: string;
  board: string;
  version: string;
  cell_bits: number;
  max_payload?: number;
}

export interface HelperError {
  code: string;
  message: string;
  phase?: number;
  detail_code?: number;
  candidates?: ConnectCandidate[];
}

export interface TextValue {
  text: string;
}

export interface WordsValue {
  words: string[];
}

export interface SeeValue {
  name: string;
  is_overlay: boolean;
  value_class: number;
  rendered: string;
}

export interface ResetValue {
  status?: number;
  heap_size: number;
  heap_used: number;
  heap_overlay_used?: number;
  slot_count?: number;
  slot_overlay_count?: number;
  version?: string;
}

export interface ControlSessionEvent {
  type: "event";
  event:
    | "connected"
    | "output"
    | "value"
    | "error"
    | "interrupted"
    | "idle"
    | "disconnected";
  request_id?: number;
  data?: string;
  value?: unknown;
  device?: DeviceInfo;
  error?: HelperError;
}

interface ControlSessionResponse {
  type: "response";
  id: number;
  ok: boolean;
  result?: unknown;
  error?: HelperError;
}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
}

export class ControlSessionClientError extends Error {
  readonly code: string;
  readonly phase?: number;
  readonly detailCode?: number;
  readonly candidates: ConnectCandidate[];

  constructor(error: HelperError) {
    super(error.message);
    this.name = "ControlSessionClientError";
    this.code = error.code;
    this.phase = error.phase;
    this.detailCode = error.detail_code;
    this.candidates = error.candidates ?? [];
  }
}

export class ControlSessionClient {
  private child: ChildProcessWithoutNullStreams | null = null;
  private buffer = "";
  private stderr = "";
  private nextId = 0;
  private readonly pending = new Map<number, PendingRequest>();
  private readonly emitter = new EventEmitter();
  private disposed = false;

  constructor(
    private readonly cliPath: string,
    private readonly cwd: string,
    private readonly spawnArgs: readonly string[] = [
      "tooling",
      "control-session",
    ],
  ) {}

  onEvent(listener: (event: ControlSessionEvent) => void): void {
    this.emitter.on("event", listener);
  }

  onExit(listener: (error: Error | null) => void): void {
    this.emitter.on("exit", listener);
  }

  async start(): Promise<void> {
    if (this.child) {
      return;
    }
    if (this.disposed) {
      throw new Error("client disposed");
    }

    await new Promise<void>((resolve, reject) => {
      const child = spawn(this.cliPath, [...this.spawnArgs], {
        cwd: this.cwd,
        stdio: "pipe",
      });

      let settled = false;

      child.on("spawn", () => {
        settled = true;
        this.child = child;
        resolve();
      });

      child.on("error", (err: Error) => {
        if (!settled) {
          settled = true;
          reject(err);
        }
      });

      child.stdout.on("data", (chunk: Buffer) => {
        this.handleStdout(chunk.toString("utf8"));
      });

      child.stderr.on("data", (chunk: Buffer) => {
        this.stderr += chunk.toString("utf8");
      });

      child.on("exit", (code, signal) => {
        this.handleExit(code, signal);
      });
    });
  }

  get isRunning(): boolean {
    return this.child !== null && !this.disposed;
  }

  async connect(port?: string): Promise<DeviceInfo> {
    const result = await this.call("connect", port ? { port } : {});
    return result as DeviceInfo;
  }

  async disconnect(): Promise<void> {
    await this.call("disconnect");
  }

  async eval(source: string): Promise<TextValue> {
    const result = await this.call("eval", { source });
    return result as TextValue;
  }

  async reset(): Promise<ResetValue> {
    const result = await this.call("reset");
    return result as ResetValue;
  }

  async interrupt(): Promise<void> {
    await this.call("interrupt");
  }

  async words(): Promise<WordsValue> {
    const result = await this.call("words");
    return result as WordsValue;
  }

  async see(name: string): Promise<SeeValue> {
    const result = await this.call("see", { name });
    return result as SeeValue;
  }

  async save(): Promise<TextValue> {
    const result = await this.call("save");
    return result as TextValue;
  }

  async restore(): Promise<TextValue> {
    const result = await this.call("restore");
    return result as TextValue;
  }

  async wipe(): Promise<TextValue> {
    const result = await this.call("wipe");
    return result as TextValue;
  }

  async core(name: string): Promise<TextValue> {
    const result = await this.call("core", { name });
    return result as TextValue;
  }

  async slotInfo(name: string): Promise<TextValue> {
    const result = await this.call("slot_info", { name });
    return result as TextValue;
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;

    if (this.child) {
      this.child.kill();
      this.child = null;
    }

    this.rejectPending(new Error("control session client disposed"));
    this.emitter.removeAllListeners();
  }

  private call(
    command: string,
    payload: Record<string, unknown> = {},
  ): Promise<unknown> {
    if (!this.child) {
      return Promise.reject(new Error("control session helper not running"));
    }

    const id = ++this.nextId;
    const line = JSON.stringify({ id, command, ...payload }) + "\n";

    return new Promise<unknown>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.child!.stdin.write(line, "utf8", (err?: Error | null) => {
        if (err) {
          this.pending.delete(id);
          reject(err);
        }
      });
    });
  }

  private handleStdout(chunk: string): void {
    this.buffer += chunk;

    for (;;) {
      const index = this.buffer.indexOf("\n");
      if (index < 0) {
        return;
      }

      const line = this.buffer.slice(0, index);
      this.buffer = this.buffer.slice(index + 1);
      if (line.length === 0) {
        continue;
      }
      this.handleLine(line);
    }
  }

  private handleLine(line: string): void {
    let message: ControlSessionResponse | ControlSessionEvent;
    try {
      message = JSON.parse(line) as ControlSessionResponse | ControlSessionEvent;
    } catch {
      return;
    }

    if (message.type === "response") {
      const pending = this.pending.get(message.id);
      if (!pending) {
        return;
      }
      this.pending.delete(message.id);
      if (message.ok) {
        pending.resolve(message.result);
        return;
      }
      pending.reject(
        new ControlSessionClientError(
          message.error ?? {
            code: "internal",
            message: "control session request failed",
          },
        ),
      );
      return;
    }

    this.emitter.emit("event", message);
  }

  private handleExit(code: number | null, signal: NodeJS.Signals | null): void {
    const detail =
      this.stderr.trim().length > 0
        ? this.stderr.trim()
        : `helper exited (${code ?? "signal"}${signal ? ` ${signal}` : ""})`;
    const error = this.disposed ? null : new Error(detail);

    this.child = null;
    this.rejectPending(error ?? new Error("control session helper exited"));
    this.emitter.emit("exit", error);
  }

  private rejectPending(error: Error): void {
    for (const pending of this.pending.values()) {
      pending.reject(error);
    }
    this.pending.clear();
  }
}
