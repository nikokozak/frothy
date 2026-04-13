import * as vscode from "vscode";
import {
  ConnectionState,
  ControllerSnapshot,
  FrothyController,
} from "./controller";
import { resolveSendSourceCommand } from "./send-file";
import {
  BufferedOutputChannel,
  VSCodeHost,
  createControlSessionClient,
  createVSCodeCliPathResolver,
} from "./vscode-host";

let activeController: FrothyController | null = null;

export interface ExtensionTestApi {
  getSnapshot(): ControllerSnapshot;
  getOutputText(): string;
  clearOutput(): void;
  enqueueInputBoxResponse(value: string | undefined): void;
  enqueueWarningResponse(value: string | undefined): void;
  waitForState(state: ConnectionState, timeoutMs?: number): Promise<void>;
}

export function activate(
  context: vscode.ExtensionContext,
): ExtensionTestApi {
  const bufferedOutput = new BufferedOutputChannel(
    vscode.window.createOutputChannel("Frothy Console"),
  );
  const statusItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    50,
  );
  const interruptItem = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    49,
  );
  const host = new VSCodeHost(context, bufferedOutput);
  const controller = new FrothyController({
    host,
    resolveCliPath: createVSCodeCliPathResolver(host),
    createClient: createControlSessionClient,
    resolveSendSource: resolveSendSourceCommand,
  });
  activeController = controller;

  const sidebarProvider = new FrothySidebarProvider(controller);
  const treeView = vscode.window.createTreeView("frothyDeviceView", {
    treeDataProvider: sidebarProvider,
  });

  const commands: Array<[string, () => Promise<void> | void]> = [
    ["frothy.connect", async () => controller.connectToDevice()],
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
    ["froth.connect", async () => controller.connectToDevice()],
    ["froth.sendSelection", () => controller.sendSelection()],
    ["froth.sendFile", () => controller.sendFile()],
    ["froth.interrupt", () => controller.interrupt()],
    ["froth.doctor", () => controller.runDoctor()],
  ];

  for (const [command, handler] of commands) {
    context.subscriptions.push(vscode.commands.registerCommand(command, handler));
  }

  context.subscriptions.push(
    statusItem,
    interruptItem,
    treeView,
    { dispose: () => bufferedOutput.dispose() },
    { dispose: () => controller.dispose() },
  );

  controller.onStateChange(() => {
    sidebarProvider.refresh();
    updateStatusBar(statusItem, interruptItem, controller.getSnapshot());
  });

  updateStatusBar(statusItem, interruptItem, controller.getSnapshot());
  statusItem.show();
  controller.start();

  const api: ExtensionTestApi = {
    getSnapshot: () => controller.getSnapshot(),
    getOutputText: () => bufferedOutput.getText(),
    clearOutput: () => bufferedOutput.clearBuffer(),
    enqueueInputBoxResponse: (value) => host.enqueueInputBoxResponse(value),
    enqueueWarningResponse: (value) => host.enqueueWarningResponse(value),
    waitForState: (state, timeoutMs = 5000) =>
      waitForState(controller, state, timeoutMs),
  };
  return api;
}

export function deactivate(): Thenable<void> | undefined {
  const controller = activeController;
  activeController = null;
  if (controller) {
    return controller.deactivate();
  }
  return undefined;
}

function updateStatusBar(
  statusItem: vscode.StatusBarItem,
  interruptItem: vscode.StatusBarItem,
  snapshot: ControllerSnapshot,
): void {
  switch (snapshot.state) {
    case "idle":
      statusItem.text = "$(circle-large-outline) Frothy: Idle";
      statusItem.tooltip =
        "Open a .frothy or .froth file and connect to a Frothy device.";
      statusItem.command = "frothy.connect";
      statusItem.backgroundColor = undefined;
      break;
    case "connecting":
      statusItem.text = "$(sync~spin) Frothy: Connecting";
      statusItem.tooltip = "Connecting to a Frothy device";
      statusItem.command = "frothy.connect";
      statusItem.backgroundColor = undefined;
      break;
    case "connected":
      if (snapshot.degradedSendFile) {
        statusItem.text = snapshot.device
          ? `$(warning) Frothy: ${snapshot.device.board} (additive)`
          : "$(warning) Frothy: Connected (additive)";
        statusItem.tooltip =
          "Send File is in additive fallback mode because the connected firmware does not support control reset.";
        statusItem.backgroundColor = new vscode.ThemeColor(
          "statusBarItem.warningBackground",
        );
      } else {
        statusItem.text = snapshot.device
          ? `$(plug) Frothy: ${snapshot.device.board}`
          : "$(plug) Frothy: Connected";
        statusItem.tooltip = snapshot.device
          ? `Connected to ${snapshot.device.board} on ${snapshot.device.port}`
          : "Connected to a Frothy device";
        statusItem.backgroundColor = undefined;
      }
      statusItem.command = "frothy.disconnect";
      break;
    case "running":
      statusItem.text = "$(sync~spin) Frothy: Running";
      statusItem.tooltip =
        "A Frothy program is running. Use Interrupt to stop it.";
      statusItem.command = "frothy.disconnect";
      statusItem.backgroundColor = undefined;
      break;
    case "disconnected":
      statusItem.text = "$(debug-disconnect) Frothy: Disconnected";
      statusItem.tooltip = "No active Frothy control session";
      statusItem.command = "frothy.connect";
      statusItem.backgroundColor = new vscode.ThemeColor(
        "statusBarItem.warningBackground",
      );
      break;
  }

  if (snapshot.state === "running") {
    interruptItem.text = "$(debug-stop) Interrupt";
    interruptItem.tooltip = "Interrupt the running Frothy program";
    interruptItem.command = "frothy.interrupt";
    interruptItem.backgroundColor = new vscode.ThemeColor(
      "statusBarItem.errorBackground",
    );
    interruptItem.color = new vscode.ThemeColor(
      "statusBarItem.errorForeground",
    );
    interruptItem.show();
  } else {
    interruptItem.hide();
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

    const snapshot = this.controller.getSnapshot();
    const items = [
      new SidebarItem("Session", snapshot.state, new vscode.ThemeIcon("pulse")),
    ];

    if (snapshot.degradedSendFile) {
      items.push(
        new SidebarItem(
          "Send File",
          "additive fallback",
          new vscode.ThemeIcon("warning"),
        ),
      );
    } else {
      items.push(
        new SidebarItem(
          "Send File",
          "reset + eval",
          new vscode.ThemeIcon("history"),
        ),
      );
    }

    if (!snapshot.device) {
      return items;
    }

    return items.concat([
      new SidebarItem(
        "Board",
        snapshot.device.board,
        new vscode.ThemeIcon("circuit-board"),
      ),
      new SidebarItem(
        "Port",
        snapshot.device.port,
        new vscode.ThemeIcon("plug"),
      ),
      new SidebarItem(
        "Version",
        snapshot.device.version,
        new vscode.ThemeIcon("tag"),
      ),
      new SidebarItem(
        "Cell Bits",
        `${snapshot.device.cell_bits}`,
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

async function waitForState(
  controller: FrothyController,
  state: ConnectionState,
  timeoutMs: number,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (controller.getSnapshot().state === state) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  throw new Error(`timed out waiting for controller state ${state}`);
}
