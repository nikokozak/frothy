import * as net from "net";
import { EventEmitter } from "events";
import { resolveDaemonSocketPath } from "./froth-paths";

// --- JSON-RPC 2.0 wire types (match daemon rpc.go) ---

interface RPCRequest {
  jsonrpc: "2.0";
  method: string;
  params?: unknown;
  id: number;
}

interface RPCError {
  code: number;
  message: string;
}

// --- Domain types (match daemon rpc.go exactly) ---

export interface EvalParams {
  source: string;
}

export interface EvalResult {
  status: number;
  error_code?: number;
  fault_word?: string;
  stack_repr?: string;
}

export interface HelloResult {
  cell_bits: number;
  max_payload: number;
  heap_size: number;
  heap_used: number;
  slot_count: number;
  version: string;
  board: string;
}

export interface InfoResult {
  heap_size: number;
  heap_used: number;
  heap_overlay_used: number;
  slot_count: number;
  slot_overlay_count: number;
  version: string;
}

export interface ResetResult {
  status: number;
  heap_size: number;
  heap_used: number;
  heap_overlay_used: number;
  slot_count: number;
  slot_overlay_count: number;
  version: string;
}

export interface StatusResult {
  pid: number;
  api_version: number;
  daemon_version: string;
  running: boolean;
  connected: boolean;
  reconnecting?: boolean;
  target: "serial" | "local";
  device?: HelloResult;
  port?: string;
}

export interface ConsoleEvent {
  data: string;
}

export interface InputWaitEvent {
  reason: number;
  seq: number;
}

export interface InputParams {
  data: string;
  seq: number;
}

export interface ConnectedEvent {
  device: HelloResult;
  port: string;
}

// Notification from the daemon. Method is one of: console, connected,
// disconnected, reconnecting. Params shape depends on method.
export interface DaemonNotification {
  method: string;
  params: unknown;
}

// --- Error types ---

const ERR_NOT_CONNECTED = -32001;

export class DaemonClientError extends Error {
  readonly code: number;

  constructor(code: number, message: string) {
    super(message);
    this.name = "DaemonClientError";
    this.code = code;
  }

  get isNotConnected(): boolean {
    return this.code === ERR_NOT_CONNECTED;
  }
}

// --- Client ---

interface PendingRequest {
  resolve: (result: unknown) => void;
  reject: (error: Error) => void;
}

/**
 * JSON-RPC 2.0 client for the Froth daemon. Mirrors the Go client
 * in tools/cli/internal/daemon/client.go.
 *
 * Protocol: newline-delimited JSON over a Unix domain socket.
 * Notifications (no id) are emitted as "notification" events.
 * Responses (with id) resolve the matching pending request.
 */
export class DaemonClient {
  private socket: net.Socket | null = null;
  private nextId = 0;
  private readonly pending = new Map<number, PendingRequest>();
  private buffer = "";
  private readonly emitter = new EventEmitter();
  private disposed = false;

  /** Register a listener for daemon notifications (console, connected, etc.) */
  onNotification(listener: (n: DaemonNotification) => void): void {
    this.emitter.on("notification", listener);
  }

  /** Register a listener for socket close (daemon gone, network error). */
  onClose(listener: () => void): void {
    this.emitter.on("close", listener);
  }

  /** Connect to the daemon's Unix domain socket. */
  connect(socketPath: string = resolveDaemonSocketPath()): Promise<void> {
    if (this.disposed) {
      return Promise.reject(new Error("client disposed"));
    }

    return new Promise<void>((resolve, reject) => {
      const sock = net.createConnection(socketPath);

      let settled = false;

      sock.on("connect", () => {
        settled = true;
        this.socket = sock;
        resolve();
      });

      sock.on("error", (err: Error) => {
        if (!settled) {
          settled = true;
          reject(err);
        }
      });

      sock.on("data", (chunk: Buffer) => {
        this.handleData(chunk.toString("utf-8"));
      });

      sock.on("close", () => {
        this.handleSocketClose();
      });
    });
  }

  get isConnected(): boolean {
    return this.socket !== null && !this.disposed;
  }

  // --- RPC convenience methods (match Go client) ---

  async hello(): Promise<HelloResult> {
    return (await this.call("hello")) as HelloResult;
  }

  async eval(source: string): Promise<EvalResult> {
    const params: EvalParams = { source };
    return (await this.call("eval", params)) as EvalResult;
  }

  async info(): Promise<InfoResult> {
    return (await this.call("info")) as InfoResult;
  }

  async reset(): Promise<ResetResult> {
    return (await this.call("reset")) as ResetResult;
  }

  async interrupt(): Promise<void> {
    await this.call("interrupt");
  }

  async sendInput(data: string | Uint8Array, seq: number): Promise<void> {
    const bytes =
      typeof data === "string" ? Buffer.from(data, "utf8") : Buffer.from(data);
    const params: InputParams = { data: bytes.toString("base64"), seq };
    await this.call("input", params);
  }

  async status(): Promise<StatusResult> {
    return (await this.call("status")) as StatusResult;
  }

  /** Tear down the client. Closes socket, rejects pending, removes listeners. */
  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;

    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }

    this.buffer = "";
    this.rejectAllPending(new Error("client disposed"));
    this.emitter.removeAllListeners();
  }

  // --- Internals ---

  private call(method: string, params?: unknown): Promise<unknown> {
    if (!this.socket) {
      return Promise.reject(new Error("not connected"));
    }

    const id = ++this.nextId;

    const request: RPCRequest = {
      jsonrpc: "2.0",
      method,
      id,
    };
    if (params !== undefined) {
      request.params = params;
    }

    return new Promise<unknown>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });

      const line = JSON.stringify(request) + "\n";
      this.socket!.write(line, (err) => {
        if (err) {
          this.pending.delete(id);
          reject(new Error(`write failed: ${err.message}`));
        }
      });
    });
  }

  private handleData(chunk: string): void {
    this.buffer += chunk;

    let idx: number;
    while ((idx = this.buffer.indexOf("\n")) !== -1) {
      const line = this.buffer.slice(0, idx);
      this.buffer = this.buffer.slice(idx + 1);
      if (line.length > 0) {
        this.handleLine(line);
      }
    }
  }

  private handleLine(line: string): void {
    let msg: Record<string, unknown>;
    try {
      msg = JSON.parse(line) as Record<string, unknown>;
    } catch {
      return;
    }

    if (typeof msg["method"] === "string" && !("id" in msg)) {
      this.emitter.emit("notification", {
        method: msg["method"],
        params: msg["params"],
      } as DaemonNotification);
      return;
    }

    if ("id" in msg && typeof msg["id"] === "number") {
      const id = msg["id"];
      const entry = this.pending.get(id);
      if (!entry) {
        return;
      }
      this.pending.delete(id);

      const rpcErr = msg["error"] as RPCError | undefined;
      if (rpcErr) {
        entry.reject(new DaemonClientError(rpcErr.code, rpcErr.message));
      } else {
        entry.resolve(msg["result"]);
      }
    }
  }

  private handleSocketClose(): void {
    this.socket = null;
    this.buffer = "";
    this.rejectAllPending(new Error("connection closed"));
    this.emitter.emit("close");
  }

  private rejectAllPending(err: Error): void {
    for (const [, entry] of this.pending) {
      entry.reject(err);
    }
    this.pending.clear();
  }
}
