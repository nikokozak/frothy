import * as vscode from "vscode";
import { cliCandidates, resolveCliCandidate } from "./cli-discovery";
import {
  ConnectCandidate,
  ControlSessionClient,
} from "./control-session-client";
import {
  ControllerHost,
  DocumentLike,
  EditorLike,
  OutputChannelLike,
  TerminalLike,
} from "./controller";

const bindingNamePattern = /[A-Za-z0-9_.]+/;

export class BufferedOutputChannel implements OutputChannelLike {
  private buffer = "";

  constructor(private readonly channel: vscode.OutputChannel) {}

  show(preserveFocus?: boolean): void {
    this.channel.show(preserveFocus);
  }

  append(value: string): void {
    this.buffer += value;
    this.channel.append(value);
  }

  appendLine(value: string): void {
    this.buffer += `${value}\n`;
    this.channel.appendLine(value);
  }

  getText(): string {
    return this.buffer;
  }

  clearBuffer(): void {
    this.buffer = "";
  }

  dispose(): void {
    this.channel.dispose();
  }
}

class VSCodeTerminal implements TerminalLike {
  constructor(private readonly terminal: vscode.Terminal) {}

  show(preserveFocus?: boolean): void {
    this.terminal.show(preserveFocus);
  }
}

class VSCodeDocument implements DocumentLike {
  constructor(private readonly document: vscode.TextDocument) {}

  get uriScheme(): string {
    return this.document.uri.scheme;
  }

  get fsPath(): string {
    return this.document.uri.fsPath;
  }

  get isDirty(): boolean {
    return this.document.isDirty;
  }

  save(): PromiseLike<boolean> {
    return this.document.save();
  }
}

class VSCodeEditor implements EditorLike {
  readonly document: DocumentLike;

  constructor(private readonly editor: vscode.TextEditor) {
    this.document = new VSCodeDocument(editor.document);
  }

  selectionText(): string {
    return this.editor.document.getText(this.editor.selection);
  }

  currentLineText(): string {
    return this.editor.document.lineAt(this.editor.selection.active.line).text;
  }

  selectedName(): string | null {
    const selection = this.selectionText().trim();
    if (selection.length > 0) {
      return selection;
    }

    const range = this.editor.document.getWordRangeAtPosition(
      this.editor.selection.active,
      bindingNamePattern,
    );
    if (!range) {
      return null;
    }
    return this.editor.document.getText(range);
  }
}

export class VSCodeHost implements ControllerHost {
  private readonly inputBoxResponses: Array<string | undefined> = [];
  private readonly warningResponses: Array<string | undefined> = [];

  constructor(
    private readonly context: vscode.ExtensionContext,
    readonly output: BufferedOutputChannel,
  ) {}

  getActiveEditor(): EditorLike | null {
    const editor = vscode.window.activeTextEditor;
    return editor ? new VSCodeEditor(editor) : null;
  }

  async showWarningMessage(
    message: string,
    ...items: string[]
  ): Promise<string | undefined> {
    if (this.warningResponses.length > 0) {
      return this.warningResponses.shift();
    }
    return vscode.window.showWarningMessage(message, ...items);
  }

  showErrorMessage(message: string): void {
    void vscode.window.showErrorMessage(message);
  }

  async showInputBox(options: {
    prompt: string;
    value: string;
    ignoreFocusOut?: boolean;
  }): Promise<string | undefined> {
    if (this.inputBoxResponses.length > 0) {
      return this.inputBoxResponses.shift();
    }
    return vscode.window.showInputBox(options);
  }

  async pickDevice(
    candidates: ConnectCandidate[],
  ): Promise<ConnectCandidate | undefined> {
    const picked = await vscode.window.showQuickPick(
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
    );
    return picked?.candidate;
  }

  createTerminal(options: {
    name: string;
    shellPath: string;
    shellArgs: string[];
  }): TerminalLike {
    return new VSCodeTerminal(vscode.window.createTerminal(options));
  }

  getConfiguredPort(): string {
    return (
      vscode.workspace.getConfiguration("frothy").get<string>("port") ?? ""
    );
  }

  getStoredPort(key: string): string | undefined {
    return this.context.workspaceState.get<string>(key);
  }

  setStoredPort(key: string, value: string): void | PromiseLike<void> {
    return this.context.workspaceState.update(key, value);
  }

  getWorkspaceCwd(): string {
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

  getWorkspaceCwdForPath(filePath: string): string {
    const uri = vscode.Uri.file(filePath);
    const folder = vscode.workspace.getWorkspaceFolder(uri);
    if (folder) {
      return folder.uri.fsPath;
    }
    return this.getWorkspaceCwd();
  }

  enqueueInputBoxResponse(value: string | undefined): void {
    this.inputBoxResponses.push(value);
  }

  enqueueWarningResponse(value: string | undefined): void {
    this.warningResponses.push(value);
  }
}

export function createVSCodeCliPathResolver(
  host: VSCodeHost,
): () => Promise<string | null> {
  return async (): Promise<string | null> => {
    const configured = vscode.workspace
      .getConfiguration("frothy")
      .get<string>("cliPath");
    if (configured && configured.trim().length > 0) {
      const resolved = resolveCliCandidate(configured.trim(), host.getWorkspaceCwd());
      if (resolved) {
        return resolved;
      }

      host.showErrorMessage(
        `Configured Frothy CLI not found: ${configured}. Install the Frothy CLI (\`froth\`) or update frothy.cliPath.`,
      );
      return null;
    }

    for (const candidate of cliCandidates(host.getWorkspaceCwd())) {
      const resolved = resolveCliCandidate(candidate, host.getWorkspaceCwd());
      if (resolved) {
        return resolved;
      }
    }

    host.showErrorMessage(
      "Frothy CLI not found. Install the Frothy CLI (`froth`) and ensure it is on PATH, or set frothy.cliPath.",
    );
    return null;
  };
}

export function createControlSessionClient(
  cliPath: string,
  cwd: string,
): ControlSessionClient {
  return new ControlSessionClient(cliPath, cwd);
}
