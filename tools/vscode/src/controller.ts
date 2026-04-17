import {
  ConnectCandidate,
  ControlSessionClientError,
  ControlSessionEvent,
  DeviceInfo,
  SeeValue,
  TextValue,
  WordsValue,
} from "./control-session-client";
import {
  PrepareSendFileResetResult,
  prepareSendFileReset,
} from "./send-file-reset";
import { splitTopLevelForms } from "./runtime-forms";

const lastPortKey = "frothy.lastPort";
const runningSettleTimeoutMs = 15000;

export type ConnectionState =
  | "idle"
  | "connecting"
  | "connected"
  | "running"
  | "disconnected";

export interface ControllerSnapshot {
  state: ConnectionState;
  device: DeviceInfo | null;
  degradedSendFile: boolean;
  lastRunPreview: string | null;
  pinnedRunPreview: string | null;
}

export type StateChangeListener = () => void;

export interface OutputChannelLike {
  show(preserveFocus?: boolean): void;
  append(value: string): void;
  appendLine(value: string): void;
}

export interface TerminalLike {
  show(preserveFocus?: boolean): void;
}

export interface DocumentLike {
  readonly uriScheme: string;
  readonly fsPath: string;
  readonly isDirty: boolean;
  save(): PromiseLike<boolean>;
}

export interface EditorLike {
  readonly document: DocumentLike;
  selectionText(): string;
  currentLineText(): string;
  currentRuntimeFormText(): string | null;
  selectedName(): string | null;
}

export interface ControllerHost {
  readonly output: OutputChannelLike;
  getActiveEditor(): EditorLike | null;
  showWarningMessage(
    message: string,
    ...items: string[]
  ): Promise<string | undefined>;
  showErrorMessage(message: string): void;
  showInputBox(options: {
    prompt: string;
    value: string;
    ignoreFocusOut?: boolean;
  }): Promise<string | undefined>;
  pickDevice(
    candidates: ConnectCandidate[],
  ): Promise<ConnectCandidate | undefined>;
  createTerminal(options: {
    name: string;
    shellPath: string;
    shellArgs: string[];
  }): TerminalLike;
  getConfiguredPort(): string;
  getStoredPort(key: string): string | undefined;
  setStoredPort(key: string, value: string): void | PromiseLike<void>;
  getWorkspaceCwd(): string;
  getWorkspaceCwdForPath(path: string): string;
}

export interface ControlSessionClientLike {
  start(): Promise<void>;
  readonly isRunning: boolean;
  onEvent(listener: (event: ControlSessionEvent) => void): void;
  onExit(listener: (error: Error | null) => void): void;
  connect(port?: string): Promise<DeviceInfo>;
  disconnect(): Promise<void>;
  eval(source: string): Promise<TextValue>;
  reset(): Promise<unknown>;
  interrupt(): Promise<void>;
  words(): Promise<WordsValue>;
  see(name: string): Promise<SeeValue>;
  save(): Promise<TextValue>;
  restore(): Promise<TextValue>;
  wipe(): Promise<TextValue>;
  core(name: string): Promise<TextValue>;
  slotInfo(name: string): Promise<TextValue>;
  dispose(): void;
}

export interface ControllerDeps {
  host: ControllerHost;
  resolveCliPath(): Promise<string | null>;
  createClient(cliPath: string, cwd: string): ControlSessionClientLike;
  resolveSendSource(
    cliPath: string,
    filePath: string,
    cwd: string,
  ): Promise<{ source: string }>;
}

export class FrothyController {
  private client: ControlSessionClientLike | null = null;
  private state: ConnectionState = "idle";
  private device: DeviceInfo | null = null;
  private disposed = false;
  private deactivating = false;
  private degradedSendFile = false;
  private lastRunSource: string | null = null;
  private pinnedRunSource: string | null = null;
  private runningOperations = 0;
  private readonly runningWaiters: Array<() => void> = [];
  private readonly stateListeners: StateChangeListener[] = [];
  private clientGeneration = 0;
  private connectAttempt = 0;

  constructor(private readonly deps: ControllerDeps) {}

  start(): void {
    this.notifyStateChange();
  }

  dispose(): void {
    this.disposed = true;
    this.disposeClient();
  }

  async deactivate(): Promise<void> {
    if (this.deactivating) {
      return;
    }
    this.deactivating = true;

    try {
      await this.disconnect();
    } catch {
      this.dispose();
    }
  }

  onStateChange(listener: StateChangeListener): void {
    this.stateListeners.push(listener);
  }

  getSnapshot(): ControllerSnapshot {
    return {
      state: this.state,
      device: this.device,
      degradedSendFile: this.degradedSendFile,
      lastRunPreview: this.lastRunSource ? previewText(this.lastRunSource) : null,
      pinnedRunPreview: this.pinnedRunSource
        ? previewText(this.pinnedRunSource)
        : null,
    };
  }

  async connectToDevice(port?: string): Promise<boolean> {
    if (this.state === "running") {
      this.deps.host.output.appendLine(
        "[frothy] reconnect requested while running; restarting control session",
      );
      await this.disconnect();
    }

    const attempt = ++this.connectAttempt;
    const client = await this.ensureClient();
    if (!client) {
      return false;
    }
    if (!this.isCurrentConnectAttempt(attempt)) {
      return this.state === "connected";
    }

    const configuredPort = this.deps.host.getConfiguredPort().trim();
    const storedPort = this.deps.host.getStoredPort(lastPortKey)?.trim() ?? "";
    const preferredPort = port ?? (configuredPort || storedPort);
    const usingStoredPort =
      port === undefined && configuredPort.length === 0 && storedPort.length > 0;
    this.setState("connecting");

    try {
      await client.connect(preferredPort || undefined);
      if (!this.isCurrentConnectAttempt(attempt)) {
        return this.state === "connected";
      }
      return true;
    } catch (err: unknown) {
      if (!this.isCurrentConnectAttempt(attempt)) {
        return this.state === "connected";
      }
      if (
        usingStoredPort &&
        (await this.retryConnectWithoutStoredPort(client, err, attempt))
      ) {
        if (!this.isCurrentConnectAttempt(attempt)) {
          return this.state === "connected";
        }
        return this.state === "connected";
      }
      const handled = await this.handleConnectError(client, err);
      if (!this.isCurrentConnectAttempt(attempt)) {
        return this.state === "connected";
      }
      if (!handled) {
        this.setState("disconnected");
      }
      return this.state === "connected";
    }
  }

  async disconnect(): Promise<void> {
    if (!this.client) {
      this.clearSessionState();
      this.setState("idle");
      return;
    }

    const client = this.client;
    this.client = null;
    this.clientGeneration += 1;
    this.connectAttempt += 1;
    const disconnectGeneration = this.clientGeneration;
    try {
      await client.disconnect();
    } catch {
      // Best effort.
    } finally {
      client.dispose();
      if (this.client === null && this.clientGeneration === disconnectGeneration) {
        this.clearSessionState();
        this.setState("idle");
      }
    }
  }

  async sendSelection(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const editor = this.deps.host.getActiveEditor();
    if (!editor) {
      await this.deps.host.showWarningMessage("No active editor");
      return;
    }

    let text: string;
    try {
      text = editor.selectionText().trim().length > 0
        ? editor.selectionText()
        : editor.currentRuntimeFormText() ?? editor.currentLineText();
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      this.deps.host.showErrorMessage(`Frothy eval failed: ${message}`);
      return;
    }
    if (text.trim().length === 0) {
      return;
    }

    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`> ${previewText(text)}`);
    if (isRepeatableRunSource(text)) {
      this.rememberLastRun(text);
    }
    await this.runTextOperation(
      "eval",
      () => this.client!.eval(text),
      true,
    );
  }

  async runBinding(): Promise<void> {
    const name = await this.resolveBindingName(
      'Zero-arity binding name to run, for example "demo.loop"',
      this.lastRunBindingName() ?? "",
    );
    if (!name) {
      return;
    }
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const source = `${name}:`;
    this.rememberLastRun(source);
    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] run ${source}`);
    await this.runTextOperation(
      "run",
      () => this.client!.eval(source),
      true,
    );
  }

  async pinRunBinding(): Promise<void> {
    const name = await this.resolveBindingName(
      'Zero-arity binding name to pin, for example "boot"',
      this.pinnedRunBindingName() ?? this.lastRunBindingName() ?? "",
    );
    if (!name) {
      return;
    }

    const source = `${name}:`;
    this.rememberPinnedRun(source);
    this.deps.host.output.appendLine(`[frothy] pinned run ${source}`);
  }

  async runLast(): Promise<void> {
    const source = this.lastRunSource;
    if (!source) {
      await this.deps.host.showWarningMessage(
        "No previous Frothy run form. Use Run Binding or Send Selection / Form on an expression first.",
      );
      return;
    }
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] rerun ${previewText(source)}`);
    const result = await this.runTextOperation(
      "rerun",
      () => this.client!.eval(source),
      true,
    );
    if (result) {
      this.rememberLastRun(source);
    }
  }

  async runPinned(): Promise<void> {
    const source = this.pinnedRunSource;
    if (!source) {
      await this.deps.host.showWarningMessage(
        "No pinned Frothy run binding. Use Pin Run Binding first.",
      );
      return;
    }
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    this.rememberLastRun(source);
    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] run pinned ${source}`);
    await this.runTextOperation(
      "run pinned",
      () => this.client!.eval(source),
      true,
    );
  }

  async sendFile(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    const preflightClient = this.client;
    const preflightGeneration = this.clientGeneration;
    if (!preflightClient) {
      await this.deps.host.showWarningMessage("No active Frothy session");
      return;
    }

    const editor = this.deps.host.getActiveEditor();
    if (!editor) {
      await this.deps.host.showWarningMessage("No active editor");
      return;
    }

    const document = editor.document;
    if (document.uriScheme !== "file") {
      await this.deps.host.showWarningMessage(
        "Save the file to disk before sending it to Frothy.",
      );
      return;
    }

    if (document.isDirty) {
      const saved = await document.save();
      if (!saved) {
        this.deps.host.showErrorMessage("Save failed. File was not sent.");
        return;
      }
    }

    const cliPath = await this.deps.resolveCliPath();
    if (!cliPath) {
      return;
    }

    let source: string;
    try {
      const result = await this.deps.resolveSendSource(
        cliPath,
        document.fsPath,
        this.deps.host.getWorkspaceCwdForPath(document.fsPath),
      );
      source = result.source;
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      this.deps.host.showErrorMessage(`Frothy send prepare failed: ${message}`);
      return;
    }

    if (!this.isCurrentClient(preflightClient, preflightGeneration)) {
      return;
    }

    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] send ${document.fsPath}`);

    if (!(await this.interruptRunningBeforeFileSend())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const resetClient = this.client;
    const resetGeneration = this.clientGeneration;
    if (!resetClient) {
      await this.deps.host.showWarningMessage("No active Frothy session");
      return;
    }

    const resetResult = await this.prepareFileSendReset(
      resetClient,
      resetGeneration,
    );
    if (!resetResult.proceed) {
      return;
    }
    if (!this.isCurrentClient(resetClient, resetGeneration)) {
      return;
    }
    this.setDegradedSendFile(resetResult.degraded);

    this.deps.host.output.appendLine(`> ${previewText(source)}`);
    await this.runSourceForms("send", source);
  }

  async interrupt(): Promise<void> {
    if (!this.client) {
      await this.deps.host.showWarningMessage("No active Frothy session");
      return;
    }

    try {
      await this.client.interrupt();
      this.deps.host.output.appendLine("[frothy] interrupt sent");
    } catch (err: unknown) {
      this.handleClientError("interrupt", err);
    }
  }

  async showWords(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const result = await this.runRequest(
      "words",
      () => this.client!.words(),
      false,
    );
    if (!result) {
      return;
    }

    const words = result as WordsValue;
    this.deps.host.output.show(true);
    this.deps.host.output.appendLine("[frothy] words");
    for (const word of words.words) {
      this.deps.host.output.appendLine(word);
    }
  }

  async showSee(): Promise<void> {
    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    await this.runNamedInspectCommand(
      "see",
      name,
      () => this.client!.see(name),
      (view) => {
        this.deps.host.output.appendLine(
          `${view.name} | ${view.is_overlay ? "overlay" : "base"} | ${valueClassName(view.value_class)}`,
        );
        this.deps.host.output.appendLine(view.rendered);
      },
    );
  }

  async showCore(): Promise<void> {
    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    await this.runNamedTextCommand("core", name, () => this.client!.core(name));
  }

  async showSlotInfo(): Promise<void> {
    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    await this.runNamedTextCommand(
      "slotInfo",
      name,
      () => this.client!.slotInfo(name),
    );
  }

  async saveSnapshot(): Promise<void> {
    await this.runBuiltinCommand("save", () => this.client!.save());
  }

  async restoreSnapshot(): Promise<void> {
    await this.runBuiltinCommand("restore", () => this.client!.restore());
  }

  async wipeSnapshot(): Promise<void> {
    const action = await this.deps.host.showWarningMessage(
      "Dangerous wipe clears the Frothy snapshot overlay on the connected device.",
      "Wipe Snapshot",
    );
    if (action !== "Wipe Snapshot") {
      return;
    }

    await this.runBuiltinCommand("wipe", () => this.client!.wipe());
  }

  async runDoctor(): Promise<void> {
    const cliPath = await this.deps.resolveCliPath();
    if (!cliPath) {
      return;
    }

    const terminal = this.deps.host.createTerminal({
      name: "Frothy Doctor",
      shellPath: cliPath,
      shellArgs: ["doctor"],
    });
    terminal.show(true);
  }

  showConsole(): void {
    this.deps.host.output.show(true);
  }

  private async runBuiltinCommand(
    label: string,
    run: () => Promise<TextValue>,
  ): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const result = await this.runTextOperation(label, run, false);
    if (!result) {
      return;
    }

    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] ${label}`);
    if (result.text !== "nil") {
      this.deps.host.output.appendLine(result.text);
    }
  }

  private async runNamedTextCommand(
    label: string,
    name: string,
    run: () => Promise<TextValue>,
  ): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const result = await this.runTextOperation(label, run, false);
    if (!result) {
      return;
    }

    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] ${label} ${name}`);
    if (result.text !== "nil") {
      this.deps.host.output.appendLine(result.text);
    }
  }

  private async runNamedInspectCommand(
    label: string,
    name: string,
    run: () => Promise<SeeValue>,
    onValue: (value: SeeValue) => void,
  ): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!(await this.requireIdle())) {
      return;
    }

    const result = await this.runRequest(label, run, false);
    if (!result) {
      return;
    }

    const view = result as SeeValue;
    this.deps.host.output.show(true);
    this.deps.host.output.appendLine(`[frothy] ${label} ${name}`);
    onValue(view);
  }

  private async runTextOperation(
    label: string,
    run: () => Promise<TextValue>,
    logResult: boolean,
  ): Promise<TextValue | null> {
    const result = await this.runRequest(label, run, true);
    if (!result) {
      return null;
    }

    const text = result as TextValue;
    if (logResult && text.text.length > 0) {
      this.deps.host.output.appendLine(text.text);
    }
    return text;
  }

  private async runSourceForms(
    label: string,
    source: string,
  ): Promise<TextValue | null> {
    let forms: string[];
    try {
      forms = splitTopLevelForms(source);
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      this.deps.host.showErrorMessage(`Frothy ${label} failed: ${message}`);
      return null;
    }

    if (forms.length === 0) {
      return { text: "nil" };
    }

    const operationClient = this.client;
    const operationGeneration = this.clientGeneration;
    if (!operationClient) {
      this.handleClientError(
        label,
        new ControlSessionClientError({
          code: "not_connected",
          message: "no Frothy device connected",
        }),
      );
      return null;
    }

    this.beginRunning();
    try {
      let lastValue: TextValue = { text: "nil" };
      for (const form of forms) {
        lastValue = await operationClient.eval(form);
        if (!this.isCurrentClient(operationClient, operationGeneration)) {
          return null;
        }
      }
      if (lastValue.text.length > 0 && lastValue.text !== "nil") {
        this.deps.host.output.appendLine(lastValue.text);
      }
      return lastValue;
    } catch (err: unknown) {
      if (!this.isCurrentClient(operationClient, operationGeneration)) {
        return null;
      }
      this.handleClientError(label, err);
      return null;
    } finally {
      this.endRunning();
    }
  }

  private async prepareFileSendReset(
    client: ControlSessionClientLike,
    generation: number,
  ): Promise<PrepareSendFileResetResult> {
    return prepareSendFileReset(
      client,
      this.deps.host.output,
      (message, ...items) => this.deps.host.showWarningMessage(message, ...items),
      (label, err) => {
        if (this.isCurrentClient(client, generation)) {
          this.handleClientError(label, err);
        }
      },
    );
  }

  private async runRequest<T>(
    label: string,
    run: () => Promise<T>,
    running: boolean,
  ): Promise<T | null> {
    const operationClient = this.client;
    const operationGeneration = this.clientGeneration;
    if (running) {
      this.beginRunning();
    }

    try {
      const result = await run();
      if (
        operationClient &&
        !this.isCurrentClient(operationClient, operationGeneration)
      ) {
        return null;
      }
      return result;
    } catch (err: unknown) {
      if (
        operationClient &&
        !this.isCurrentClient(operationClient, operationGeneration)
      ) {
        return null;
      }
      this.handleClientError(label, err);
      return null;
    } finally {
      if (running) {
        this.endRunning();
      }
    }
  }

  private async interruptRunningBeforeFileSend(): Promise<boolean> {
    if (this.state !== "running") {
      return true;
    }
    if (!this.client) {
      await this.deps.host.showWarningMessage("No active Frothy session");
      return false;
    }

    this.deps.host.output.appendLine(
      "[frothy] send requested while running; interrupting current program",
    );
    try {
      await this.client.interrupt();
      this.deps.host.output.appendLine("[frothy] interrupt sent");
    } catch (err: unknown) {
      this.handleClientError("interrupt", err);
      return false;
    }

    if (await this.waitForRunningToFinish(runningSettleTimeoutMs)) {
      return true;
    }

    this.deps.host.output.appendLine(
      "[frothy] interrupt did not settle; restarting control session",
    );
    await this.disconnect();
    if (!(await this.waitForRunningToFinish(3000))) {
      this.deps.host.showErrorMessage(
        "Frothy send failed: running program did not stop after interrupt.",
      );
      return false;
    }
    return this.connectToDevice();
  }

  private async ensureConnected(): Promise<boolean> {
    if (this.state === "connected" || this.state === "running") {
      return true;
    }
    return this.connectToDevice();
  }

  private async requireIdle(): Promise<boolean> {
    if (this.state === "running") {
      await this.deps.host.showWarningMessage(
        "Target is running a program. Use Interrupt first.",
      );
      return false;
    }
    return true;
  }

  private async ensureClient(): Promise<ControlSessionClientLike | null> {
    if (this.client) {
      return this.client;
    }

    const cliPath = await this.deps.resolveCliPath();
    if (!cliPath) {
      return null;
    }

    const client = this.deps.createClient(cliPath, this.deps.host.getWorkspaceCwd());
    const generation = ++this.clientGeneration;
    client.onEvent((event) => this.handleHelperEvent(client, generation, event));
    client.onExit((error) => this.handleHelperExit(client, generation, error));

    try {
      await client.start();
      this.client = client;
      return client;
    } catch (err: unknown) {
      client.dispose();
      this.client = null;
      const message = err instanceof Error ? err.message : String(err);
      this.deps.host.showErrorMessage(
        `Failed to start Frothy control helper: ${message}`,
      );
      return null;
    }
  }

  private async handleConnectError(
    client: ControlSessionClientLike,
    err: unknown,
  ): Promise<boolean> {
    if (!(err instanceof ControlSessionClientError)) {
      const message = err instanceof Error ? err.message : String(err);
      this.deps.host.showErrorMessage(`Connect failed: ${message}`);
      return false;
    }

    if (err.code === "multiple_devices" && err.candidates.length > 0) {
      const picked = await this.deps.host.pickDevice(err.candidates);
      if (!picked) {
        this.clearSessionState();
        this.setState("disconnected");
        return true;
      }
      return this.connectToDevice(picked.port);
    }

    if (err.code === "no_devices") {
      this.clearSessionState();
      this.setState("disconnected");
      const action = await this.deps.host.showWarningMessage(
        "No Frothy device found.",
        "Run Doctor",
      );
      if (action === "Run Doctor") {
        await this.runDoctor();
      }
      return true;
    }

    this.handleClientError("connect", err);
    if (!client.isRunning) {
      this.disposeClient();
    }
    return false;
  }

  private async retryConnectWithoutStoredPort(
    client: ControlSessionClientLike,
    err: unknown,
    attempt: number,
  ): Promise<boolean> {
    if (
      !(err instanceof ControlSessionClientError) ||
      err.code === "multiple_devices"
    ) {
      return false;
    }

    await this.deps.host.setStoredPort(lastPortKey, "");
    this.deps.host.output.appendLine(
      "[frothy] remembered port failed; retrying device discovery",
    );
    try {
      await client.connect();
      if (!this.isCurrentConnectAttempt(attempt)) {
        return true;
      }
      return true;
    } catch (retryErr: unknown) {
      if (!this.isCurrentConnectAttempt(attempt)) {
        return true;
      }
      const handled = await this.handleConnectError(client, retryErr);
      if (!this.isCurrentConnectAttempt(attempt)) {
        return true;
      }
      if (!handled) {
        this.setState("disconnected");
      }
      return true;
    }
  }

  private handleHelperEvent(
    client: ControlSessionClientLike,
    generation: number,
    event: ControlSessionEvent,
  ): void {
    if (!this.isCurrentClient(client, generation)) {
      return;
    }

    switch (event.event) {
      case "connected":
        if (event.device) {
          this.device = event.device;
          this.setDegradedSendFile(false);
          this.rememberPort(event.device.port);
          this.deps.host.output.appendLine(
            `[frothy] connected: ${event.device.board} (${event.device.port})`,
          );
        }
        this.setState("connected");
        break;
      case "output":
        if (event.data) {
          this.deps.host.output.append(
            renderConsoleBytes(Buffer.from(event.data, "base64")),
          );
        }
        break;
      case "idle":
        if (this.state === "running" && this.runningOperations === 0) {
          this.setState(this.device ? "connected" : "disconnected");
        }
        break;
      case "interrupted":
        this.deps.host.output.appendLine("[frothy] interrupted");
        break;
      case "disconnected":
        this.deps.host.output.appendLine("[frothy] disconnected");
        this.clearSessionState();
        this.setState("disconnected");
        break;
      case "error":
      case "value":
        break;
    }
  }

  private handleHelperExit(
    client: ControlSessionClientLike,
    generation: number,
    error: Error | null,
  ): void {
    if (this.disposed || !this.isCurrentClient(client, generation)) {
      return;
    }

    this.client = null;
    this.clearSessionState();
    if (error) {
      this.deps.host.output.appendLine(`[frothy] helper exited: ${error.message}`);
    }
    this.setState("disconnected");
  }

  private handleClientError(label: string, err: unknown): void {
    if (err instanceof ControlSessionClientError) {
      if (err.code === "interrupted") {
        this.deps.host.output.appendLine("[frothy] interrupted");
        return;
      }
      if (err.code === "not_connected") {
        this.clearSessionState();
        this.setState("disconnected");
        void this.deps.host.showWarningMessage("No Frothy device connected.");
        return;
      }

      this.deps.host.output.appendLine(`[frothy] ${label}: ${err.message}`);
      this.deps.host.showErrorMessage(`Frothy ${label} failed: ${err.message}`);
      return;
    }

    const message = err instanceof Error ? err.message : String(err);
    this.deps.host.output.appendLine(`[frothy] ${label}: ${message}`);
    this.deps.host.showErrorMessage(`Frothy ${label} failed: ${message}`);
  }

  private setState(state: ConnectionState): void {
    this.state = state;
    this.notifyStateChange();
  }

  private beginRunning(): void {
    this.runningOperations += 1;
    this.setState("running");
  }

  private endRunning(): void {
    if (this.runningOperations > 0) {
      this.runningOperations -= 1;
    }
    if (this.runningOperations > 0) {
      return;
    }

    const waiters = this.runningWaiters.splice(0);
    for (const waiter of waiters) {
      waiter();
    }

    if (this.state === "running") {
      this.setState(this.device ? "connected" : "disconnected");
    }
  }

  private waitForRunningToFinish(timeoutMs: number): Promise<boolean> {
    if (this.runningOperations === 0) {
      return Promise.resolve(true);
    }

    return new Promise((resolve) => {
      let resolved = false;
      const done = (value: boolean) => {
        if (resolved) {
          return;
        }
        resolved = true;
        clearTimeout(timer);
        resolve(value);
      };

      const timer = setTimeout(() => done(false), timeoutMs);
      this.runningWaiters.push(() => done(true));
    });
  }

  private setDegradedSendFile(value: boolean): void {
    if (this.degradedSendFile === value) {
      return;
    }
    this.degradedSendFile = value;
    this.notifyStateChange();
  }

  private async resolveBindingName(
    prompt = 'Binding name, for example "save" or "board.led.pin"',
    fallbackValue = "",
  ): Promise<string | null> {
    const editor = this.deps.host.getActiveEditor();
    const selected = editor?.selectedName() ?? null;
    const value = await this.deps.host.showInputBox({
      prompt,
      value: selected ?? fallbackValue,
      ignoreFocusOut: true,
    });

    if (value === undefined) {
      return null;
    }
    const trimmed = value.trim();
    return trimmed.length > 0 ? trimmed : null;
  }

  private rememberLastRun(source: string): void {
    const trimmed = source.trim();
    if (trimmed.length === 0 || this.lastRunSource === trimmed) {
      return;
    }
    this.lastRunSource = trimmed;
    this.notifyStateChange();
  }

  private lastRunBindingName(): string | null {
    return runBindingName(this.lastRunSource);
  }

  private rememberPinnedRun(source: string): void {
    const trimmed = source.trim();
    if (trimmed.length === 0 || this.pinnedRunSource === trimmed) {
      return;
    }
    this.pinnedRunSource = trimmed;
    this.notifyStateChange();
  }

  private pinnedRunBindingName(): string | null {
    return runBindingName(this.pinnedRunSource);
  }

  private rememberPort(port: string): void {
    if (port.length === 0 || port === "stdin/stdout") {
      return;
    }
    void this.deps.host.setStoredPort(lastPortKey, port);
  }

  private disposeClient(): void {
    if (this.client) {
      this.clientGeneration += 1;
      this.client.dispose();
      this.client = null;
    }
    this.clearSessionState();
  }

  private isCurrentClient(
    client: ControlSessionClientLike,
    generation: number,
  ): boolean {
    return this.client === client && this.clientGeneration === generation;
  }

  private isCurrentConnectAttempt(attempt: number): boolean {
    return this.connectAttempt === attempt;
  }

  private clearSessionState(): void {
    this.device = null;
    this.degradedSendFile = false;
  }

  private notifyStateChange(): void {
    for (const listener of this.stateListeners) {
      listener();
    }
  }
}

function previewText(source: string): string {
  const compact = source.replace(/\s+/g, " ").trim();
  if (compact.length <= 80) {
    return compact;
  }
  return compact.slice(0, 77) + "...";
}

function runBindingName(source: string | null): string | null {
  const trimmed = source?.trim() ?? "";
  const match = trimmed.match(/^([A-Za-z_][A-Za-z0-9_.!?@]*)\s*:\s*$/);
  return match ? match[1] : null;
}

function isRepeatableRunSource(source: string): boolean {
  let forms: string[];
  try {
    forms = splitTopLevelForms(source);
  } catch {
    return false;
  }
  if (forms.length !== 1) {
    return false;
  }

  const form = forms[0].trim();
  if (/^to\s+/.test(form) || /^set\s+/.test(form)) {
    return false;
  }
  if (/^[A-Za-z_][A-Za-z0-9_.!?@]*\s*(?:\([^)]*\)\s*)?[\[{]/.test(form)) {
    return false;
  }
  return !/^[A-Za-z_][A-Za-z0-9_.!?@]*\s*(?:=|\bis\b)/.test(form);
}

export function valueClassName(valueClass: number): string {
  switch (valueClass) {
    case 0:
      return "int";
    case 1:
      return "bool";
    case 2:
      return "nil";
    case 3:
      return "text";
    case 4:
      return "cells";
    case 5:
      return "code";
    case 6:
      return "native";
    default:
      return "value";
  }
}

function renderConsoleBytes(data: Buffer): string {
  let out = "";
  const utf8 = new TextDecoder("utf-8", { fatal: true });

  for (let i = 0; i < data.length; ) {
    const byte = data[i];
    if (byte === 0x0a || byte === 0x0d || byte === 0x09) {
      out += String.fromCharCode(byte);
      i++;
      continue;
    }
    if (byte >= 0x20 && byte <= 0x7e) {
      out += String.fromCharCode(byte);
      i++;
      continue;
    }

    let decoded = false;
    for (let width = 4; width >= 2; width--) {
      if (i + width > data.length) {
        continue;
      }
      try {
        out += utf8.decode(data.subarray(i, i + width));
        i += width;
        decoded = true;
        break;
      } catch {
        // Keep looking for a valid UTF-8 slice.
      }
    }

    if (decoded) {
      continue;
    }

    out += `\\x${byte.toString(16).padStart(2, "0")}`;
    i++;
  }

  return out;
}
