import * as vscode from "vscode";
import { cliCandidates, resolveCliCandidate } from "./cli-discovery";
import {
  ConnectCandidate,
  ControlSessionClient,
  ControlSessionClientError,
  ControlSessionEvent,
  DeviceInfo,
  SeeValue,
  TextValue,
  WordsValue,
} from "./control-session-client";
import { prepareSendFileReset } from "./send-file-reset";
import { resolveSendSourceCommand } from "./send-file";

const lastPortKey = "frothy.lastPort";

type ConnectionState =
  | "idle"
  | "connecting"
  | "connected"
  | "running"
  | "disconnected";

type StateChangeListener = () => void;

let activeController: FrothyController | null = null;

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel("Frothy Console");
  const statusItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    50,
  );
  const interruptItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    49,
  );

  const controller = new FrothyController(
    context,
    output,
    statusItem,
    interruptItem,
  );
  activeController = controller;

  const sidebarProvider = new FrothySidebarProvider(controller);
  const treeView = vscode.window.createTreeView("frothyDeviceView", {
    treeDataProvider: sidebarProvider,
  });

  const commands: Array<[string, () => Promise<void> | void]> = [
    ["frothy.connect", async () => { await controller.connectToDevice(); }],
    ["frothy.disconnect", () => controller.disconnect()],
    ["frothy.sendSelection", () => controller.sendSelection()],
    ["frothy.sendFile", () => controller.sendFile()],
    ["frothy.interrupt", () => controller.interrupt()],
    ["frothy.words", () => controller.showWords()],
    ["frothy.see", () => controller.showSee()],
    ["frothy.core", () => controller.showCore()],
    ["frothy.slotInfo", () => controller.showSlotInfo()],
    ["frothy.save", () => controller.saveSnapshot()],
    ["frothy.restore", () => controller.restoreSnapshot()],
    ["frothy.wipe", () => controller.wipeSnapshot()],
    ["frothy.doctor", () => controller.runDoctor()],
    ["frothy.showConsole", () => controller.showConsole()],
    ["froth.connect", async () => { await controller.connectToDevice(); }],
    ["froth.sendSelection", () => controller.sendSelection()],
    ["froth.sendFile", () => controller.sendFile()],
    ["froth.interrupt", () => controller.interrupt()],
    ["froth.doctor", () => controller.runDoctor()],
  ];

  for (const [command, handler] of commands) {
    context.subscriptions.push(vscode.commands.registerCommand(command, handler));
  }

  context.subscriptions.push(
    output,
    statusItem,
    interruptItem,
    treeView,
    { dispose: () => controller.dispose() },
  );

  controller.onStateChange(() => sidebarProvider.refresh());
  statusItem.show();
  controller.start();
}

export function deactivate(): Thenable<void> | undefined {
  const controller = activeController;
  activeController = null;
  if (controller) {
    return controller.deactivate();
  }
  return undefined;
}

class FrothyController {
  private client: ControlSessionClient | null = null;
  private state: ConnectionState = "idle";
  private device: DeviceInfo | null = null;
  private disposed = false;
  private deactivating = false;
  private cliPathCache: string | null = null;
  private readonly stateListeners: StateChangeListener[] = [];

  constructor(
    private readonly context: vscode.ExtensionContext,
    private readonly output: vscode.OutputChannel,
    private readonly statusItem: vscode.StatusBarItem,
    private readonly interruptItem: vscode.StatusBarItem,
  ) {
    this.updateStatusBar();
  }

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

  getState(): ConnectionState {
    return this.state;
  }

  getDevice(): DeviceInfo | null {
    return this.device;
  }

  async connectToDevice(port?: string): Promise<boolean> {
    if (this.state === "running") {
      vscode.window.showWarningMessage(
        "A Frothy program is running. Interrupt it before reconnecting.",
      );
      return false;
    }

    const client = await this.ensureClient();
    if (!client) {
      return false;
    }

    const preferredPort = port ?? this.preferredPort();
    this.setState("connecting");

    try {
      await client.connect(preferredPort || undefined);
      return true;
    } catch (err: unknown) {
      const handled = await this.handleConnectError(client, err);
      if (!handled) {
        this.setState("disconnected");
      }
      return false;
    }
  }

  async disconnect(): Promise<void> {
    if (!this.client) {
      this.device = null;
      this.setState("idle");
      return;
    }

    try {
      await this.client.disconnect();
    } catch {
      // Best effort. The client is still disposed below.
    } finally {
      this.disposeClient();
      this.setState("idle");
    }
  }

  async sendSelection(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showWarningMessage("No active editor");
      return;
    }

    let text: string;
    if (editor.selection.isEmpty) {
      text = editor.document.lineAt(editor.selection.active.line).text;
    } else {
      text = editor.document.getText(editor.selection);
    }

    if (text.trim().length === 0) {
      return;
    }

    this.output.show(true);
    this.output.appendLine(`> ${previewText(text)}`);
    await this.runTextOperation("eval", () => this.client!.eval(text), true);
  }

  async sendFile(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      vscode.window.showWarningMessage("No active editor");
      return;
    }

    const document = editor.document;
    if (document.uri.scheme !== "file") {
      vscode.window.showWarningMessage(
        "Save the file to disk before sending it to Frothy.",
      );
      return;
    }

    if (document.isDirty) {
      const saved = await document.save();
      if (!saved) {
        vscode.window.showErrorMessage("Save failed. File was not sent.");
        return;
      }
    }

    const cliPath = await this.getCliPath();
    if (!cliPath) {
      return;
    }

    let source: string;
    try {
      source = await this.resolveSendSource(
        cliPath,
        document.uri.fsPath,
        this.workspaceCwdForUri(document.uri),
      );
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Frothy send prepare failed: ${message}`);
      return;
    }

    this.output.show(true);
    this.output.appendLine(`[frothy] send ${document.uri.fsPath}`);

    const resetReady = await this.prepareFileSendReset();
    if (!resetReady) {
      return;
    }

    this.output.appendLine(`> ${previewText(source)}`);
    await this.runTextOperation("send", () => this.client!.eval(source), true);
  }

  async interrupt(): Promise<void> {
    if (!this.client) {
      vscode.window.showWarningMessage("No active Frothy session");
      return;
    }

    try {
      await this.client.interrupt();
      this.output.appendLine("[frothy] interrupt sent");
    } catch (err: unknown) {
      this.handleClientError("interrupt", err);
    }
  }

  async showWords(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
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
    this.output.show(true);
    this.output.appendLine("[frothy] words");
    for (const word of words.words) {
      this.output.appendLine(word);
    }
  }

  async showSee(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    this.output.show(true);
    const infoResult = await this.runRequest(
      "slotInfo",
      () => this.client!.slotInfo(name),
      false,
    );
    if (!infoResult) {
      return;
    }

    const result = await this.runRequest(
      "see",
      () => this.client!.see(name),
      false,
    );
    if (!result) {
      return;
    }

    const view = result as SeeValue;
    this.appendInspectRendered("see", view.rendered);
  }

  async showCore(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    this.output.show(true);
    await this.runRequest(
      "core",
      () => this.client!.core(name),
      false,
    );
  }

  async showSlotInfo(): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const name = await this.resolveBindingName();
    if (!name) {
      return;
    }

    this.output.show(true);
    await this.runRequest(
      "slotInfo",
      () => this.client!.slotInfo(name),
      false,
    );
  }

  async saveSnapshot(): Promise<void> {
    await this.runBuiltinCommand("save", () => this.client!.save());
  }

  async restoreSnapshot(): Promise<void> {
    await this.runBuiltinCommand("restore", () => this.client!.restore());
  }

  async wipeSnapshot(): Promise<void> {
    await this.runBuiltinCommand("wipe", () => this.client!.wipe());
  }

  async runDoctor(): Promise<void> {
    const cliPath = await this.getCliPath();
    if (!cliPath) {
      return;
    }

    const terminal = vscode.window.createTerminal({
      name: "Frothy Doctor",
      shellPath: cliPath,
      shellArgs: ["doctor"],
    });
    terminal.show(true);
  }

  async showConsole(): Promise<void> {
    this.output.show(true);
  }

  private async runBuiltinCommand(
    label: string,
    run: () => Promise<TextValue>,
  ): Promise<void> {
    if (!(await this.ensureConnected())) {
      return;
    }
    if (!this.requireIdle()) {
      return;
    }

    const result = await this.runTextOperation(label, run, false);
    if (!result) {
      return;
    }

    this.output.show(true);
    this.output.appendLine(`[frothy] ${label}`);
    if (result.text !== "nil") {
      this.output.appendLine(result.text);
    }
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
      this.output.appendLine(text.text);
    }
    return text;
  }

  private async prepareFileSendReset(): Promise<boolean> {
    return prepareSendFileReset(
      this.client!,
      this.output,
      vscode.window.showWarningMessage,
      (label, err) => this.handleClientError(label, err),
    );
  }

  private async runRequest<T>(
    label: string,
    run: () => Promise<T>,
    running: boolean,
  ): Promise<T | null> {
    if (running) {
      this.setState("running");
    }

    try {
      return await run();
    } catch (err: unknown) {
      this.handleClientError(label, err);
      return null;
    } finally {
      if (this.state === "running") {
        this.setState(this.device ? "connected" : "disconnected");
      }
    }
  }

  private appendInspectRendered(label: string, text: string): void {
    const lines = text.split(/\r?\n/);

    if (lines.length === 0 || (lines.length === 1 && lines[0].length === 0)) {
      this.output.appendLine(`  ${label}:`);
      return;
    }

    this.output.appendLine(`  ${label}: ${lines[0]}`);
    for (const line of lines.slice(1)) {
      this.output.appendLine(`    ${line}`);
    }
  }

  private async ensureConnected(): Promise<boolean> {
    if (this.state === "connected" || this.state === "running") {
      return true;
    }
    return this.connectToDevice();
  }

  private requireIdle(): boolean {
    if (this.state === "running") {
      vscode.window.showWarningMessage(
        "Target is running a program. Use Interrupt first.",
      );
      return false;
    }
    return true;
  }

  private async ensureClient(): Promise<ControlSessionClient | null> {
    if (this.client) {
      return this.client;
    }

    const cliPath = await this.getCliPath();
    if (!cliPath) {
      return null;
    }

    const client = new ControlSessionClient(cliPath, this.workspaceCwd());
    client.onEvent((event) => this.handleHelperEvent(event));
    client.onExit((error) => this.handleHelperExit(error));

    try {
      await client.start();
      this.client = client;
      return client;
    } catch (err: unknown) {
      client.dispose();
      this.client = null;
      const message = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(
        `Failed to start Frothy control helper: ${message}`,
      );
      return null;
    }
  }

  private async handleConnectError(
    client: ControlSessionClient,
    err: unknown,
  ): Promise<boolean> {
    if (!(err instanceof ControlSessionClientError)) {
      const message = err instanceof Error ? err.message : String(err);
      vscode.window.showErrorMessage(`Connect failed: ${message}`);
      return false;
    }

    if (err.code === "multiple_devices" && err.candidates.length > 0) {
      const picked = await this.pickDevice(err.candidates);
      if (!picked) {
        this.setState("disconnected");
        return true;
      }
      return this.connectToDevice(picked.port);
    }

    if (err.code === "no_devices") {
      this.setState("disconnected");
      const action = await vscode.window.showWarningMessage(
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

  private handleHelperEvent(event: ControlSessionEvent): void {
    switch (event.event) {
      case "connected":
        if (event.device) {
          this.device = event.device;
          this.rememberPort(event.device.port);
          this.output.appendLine(
            `[frothy] connected: ${event.device.board} (${event.device.port})`,
          );
        }
        this.setState("connected");
        break;
      case "output":
        if (event.data) {
          this.output.append(this.renderConsoleBytes(Buffer.from(event.data, "base64")));
        }
        break;
      case "idle":
        if (this.state === "running") {
          this.setState(this.device ? "connected" : "disconnected");
        }
        break;
      case "interrupted":
        this.output.appendLine("[frothy] interrupted");
        break;
      case "disconnected":
        this.output.appendLine("[frothy] disconnected");
        this.device = null;
        this.setState("disconnected");
        break;
      case "error":
      case "value":
        break;
    }
  }

  private handleHelperExit(error: Error | null): void {
    if (this.disposed) {
      return;
    }

    this.client = null;
    this.device = null;
    if (error) {
      this.output.appendLine(`[frothy] helper exited: ${error.message}`);
    }
    this.setState("disconnected");
  }

  private handleClientError(label: string, err: unknown): void {
    if (err instanceof ControlSessionClientError) {
      if (err.code === "interrupted") {
        this.output.appendLine("[frothy] interrupted");
        return;
      }
      if (err.code === "not_connected") {
        this.setState("disconnected");
        vscode.window.showWarningMessage("No Frothy device connected.");
        return;
      }

      this.output.appendLine(`[frothy] ${label}: ${err.message}`);
      vscode.window.showErrorMessage(`Frothy ${label} failed: ${err.message}`);
      return;
    }

    const message = err instanceof Error ? err.message : String(err);
    this.output.appendLine(`[frothy] ${label}: ${message}`);
    vscode.window.showErrorMessage(`Frothy ${label} failed: ${message}`);
  }

  private setState(state: ConnectionState): void {
    this.state = state;
    this.updateStatusBar();
    this.notifyStateChange();
  }

  private updateStatusBar(): void {
    switch (this.state) {
      case "idle":
        this.statusItem.text = "$(circle-large-outline) Frothy: Idle";
        this.statusItem.tooltip =
          "Open a .frothy or .froth file and connect to a Frothy device.";
        this.statusItem.command = "frothy.connect";
        this.statusItem.backgroundColor = undefined;
        break;
      case "connecting":
        this.statusItem.text = "$(sync~spin) Frothy: Connecting";
        this.statusItem.tooltip = "Connecting to a Frothy device";
        this.statusItem.command = "frothy.connect";
        this.statusItem.backgroundColor = undefined;
        break;
      case "connected":
        this.statusItem.text = this.device
          ? `$(plug) Frothy: ${this.device.board}`
          : "$(plug) Frothy: Connected";
        this.statusItem.tooltip = this.device
          ? `Connected to ${this.device.board} on ${this.device.port}`
          : "Connected to a Frothy device";
        this.statusItem.command = "frothy.disconnect";
        this.statusItem.backgroundColor = undefined;
        break;
      case "running":
        this.statusItem.text = "$(sync~spin) Frothy: Running";
        this.statusItem.tooltip =
          "A Frothy program is running. Use Interrupt to stop it.";
        this.statusItem.command = "frothy.disconnect";
        this.statusItem.backgroundColor = undefined;
        break;
      case "disconnected":
        this.statusItem.text = "$(debug-disconnect) Frothy: Disconnected";
        this.statusItem.tooltip = "No active Frothy control session";
        this.statusItem.command = "frothy.connect";
        this.statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.warningBackground",
        );
        break;
    }

    if (this.state === "running") {
      this.interruptItem.text = "$(debug-stop) Interrupt";
      this.interruptItem.tooltip = "Interrupt the running Frothy program";
      this.interruptItem.command = "frothy.interrupt";
      this.interruptItem.backgroundColor = new vscode.ThemeColor(
        "statusBarItem.errorBackground",
      );
      this.interruptItem.color = new vscode.ThemeColor(
        "statusBarItem.errorForeground",
      );
      this.interruptItem.show();
    } else {
      this.interruptItem.hide();
    }
  }

  private async resolveBindingName(): Promise<string | null> {
    const selected = this.selectedNameFromEditor();
    const value = await vscode.window.showInputBox({
      prompt: 'Binding name, for example "save" or "board.led.pin"',
      value: selected ?? "",
      ignoreFocusOut: true,
    });

    if (value === undefined) {
      return null;
    }
    const trimmed = value.trim();
    return trimmed.length > 0 ? trimmed : null;
  }

  private selectedNameFromEditor(): string | null {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
      return null;
    }

    const selection = editor.document.getText(editor.selection).trim();
    if (selection.length > 0) {
      return selection;
    }

    const range = editor.document.getWordRangeAtPosition(
      editor.selection.active,
      /[A-Za-z0-9_.]+/,
    );
    if (!range) {
      return null;
    }
    return editor.document.getText(range);
  }

  private preferredPort(): string {
    const configured = vscode.workspace
      .getConfiguration("frothy")
      .get<string>("port");
    if (configured && configured.trim().length > 0) {
      return configured.trim();
    }

    const stored = this.context.workspaceState.get<string>(lastPortKey);
    return stored?.trim() ?? "";
  }

  private rememberPort(port: string): void {
    if (port.length === 0 || port === "stdin/stdout") {
      return;
    }
    void this.context.workspaceState.update(lastPortKey, port);
  }

  private async pickDevice(
    candidates: ConnectCandidate[],
  ): Promise<ConnectCandidate | undefined> {
    return vscode.window.showQuickPick(
      candidates.map((candidate) => ({
        label: candidate.board
          ? `${candidate.board} (${candidate.port})`
          : candidate.port,
        description: candidate.version ?? "",
        candidate,
      })),
      {
        placeHolder: "Select a Frothy device",
      },
    ).then((pick) => pick?.candidate);
  }

  private async getCliPath(): Promise<string | null> {
    if (this.cliPathCache) {
      return this.cliPathCache;
    }

    const configured = vscode.workspace
      .getConfiguration("frothy")
      .get<string>("cliPath");
    if (configured && configured.trim().length > 0) {
      const resolved = this.resolveCliCandidate(configured.trim());
      if (resolved) {
        this.cliPathCache = resolved;
        return resolved;
      }

      vscode.window.showErrorMessage(
        `Configured Frothy CLI not found: ${configured}. Install froth or update frothy.cliPath.`,
      );
      return null;
    }

    for (const candidate of cliCandidates(this.workspaceCwd())) {
      const resolved = this.resolveCliCandidate(candidate);
      if (resolved) {
        this.cliPathCache = resolved;
        return resolved;
      }
    }

    vscode.window.showErrorMessage(
      "Frothy CLI not found. Install `froth` and ensure it is on PATH, or set frothy.cliPath.",
    );
    return null;
  }

  private resolveCliCandidate(candidate: string): string | null {
    return resolveCliCandidate(candidate, this.workspaceCwd());
  }

  private async resolveSendSource(
    cliPath: string,
    filePath: string,
    cwd: string,
  ): Promise<string> {
    const result = await resolveSendSourceCommand(cliPath, filePath, cwd);
    return result.source;
  }

  private workspaceCwd(): string {
    const editor = vscode.window.activeTextEditor;
    if (editor) {
      const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
      if (folder) {
        return folder.uri.fsPath;
      }
    }

    const firstFolder = vscode.workspace.workspaceFolders?.[0];
    if (firstFolder) {
      return firstFolder.uri.fsPath;
    }

    return process.cwd();
  }

  private workspaceCwdForUri(uri: vscode.Uri): string {
    const folder = vscode.workspace.getWorkspaceFolder(uri);
    if (folder) {
      return folder.uri.fsPath;
    }
    return this.workspaceCwd();
  }

  private disposeClient(): void {
    if (this.client) {
      this.client.dispose();
      this.client = null;
    }
    this.device = null;
  }

  private notifyStateChange(): void {
    for (const listener of this.stateListeners) {
      listener();
    }
  }

  private renderConsoleBytes(data: Buffer): string {
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
}

class FrothySidebarProvider implements vscode.TreeDataProvider<SidebarItem> {
  private readonly changeEmitter = new vscode.EventEmitter<
    SidebarItem | undefined
  >();

  readonly onDidChangeTreeData = this.changeEmitter.event;

  constructor(private readonly controller: FrothyController) {}

  refresh(): void {
    this.changeEmitter.fire(undefined);
  }

  getTreeItem(element: SidebarItem): vscode.TreeItem {
    return element;
  }

  getChildren(element?: SidebarItem): SidebarItem[] {
    if (element) {
      return [];
    }

    const state = this.controller.getState();
    const device = this.controller.getDevice();
    const items = [
      new SidebarItem("Session", state, new vscode.ThemeIcon("pulse")),
    ];

    if (!device) {
      return items;
    }

    return items.concat([
      new SidebarItem("Board", device.board, new vscode.ThemeIcon("circuit-board")),
      new SidebarItem("Port", device.port, new vscode.ThemeIcon("plug")),
      new SidebarItem("Version", device.version, new vscode.ThemeIcon("tag")),
      new SidebarItem(
        "Cell Bits",
        `${device.cell_bits}`,
        new vscode.ThemeIcon("symbol-numeric"),
      ),
    ]);
  }
}

class SidebarItem extends vscode.TreeItem {
  constructor(
    label: string,
    description: string,
    icon: vscode.ThemeIcon,
  ) {
    super(label, vscode.TreeItemCollapsibleState.None);
    this.description = description;
    this.iconPath = icon;
  }
}

function previewText(source: string): string {
  const compact = source.replace(/\s+/g, " ").trim();
  if (compact.length <= 80) {
    return compact;
  }
  return compact.slice(0, 77) + "...";
}
