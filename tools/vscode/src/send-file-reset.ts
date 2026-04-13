import { ControlSessionClientError } from "./control-session-client";

export const resetLogLine = "[frothy] reset";
export const resetUnavailableLogLine =
  "[frothy] control reset unavailable on connected firmware; sending file additively (unsafe)";
export const resetUnavailableWarning =
  "The connected Frothy firmware does not support control reset. Send File will append definitions additively and may leave deleted bindings behind.";
export const resetUnavailableAction = "Send Anyway";

type ResetClient = {
  reset(): Promise<unknown>;
};

type ResetOutput = {
  appendLine(line: string): void;
};

type WarningFn = (
  message: string,
  ...items: string[]
) => PromiseLike<string | undefined> | string | undefined;

type ErrorFn = (label: string, err: unknown) => void;

export async function prepareSendFileReset(
  client: ResetClient,
  output: ResetOutput,
  showWarningMessage: WarningFn,
  handleError: ErrorFn,
): Promise<boolean> {
  try {
    await client.reset();
    output.appendLine(resetLogLine);
    return true;
  } catch (err: unknown) {
    if (
      err instanceof ControlSessionClientError &&
      err.code === "reset_unavailable"
    ) {
      output.appendLine(resetUnavailableLogLine);
      const action = await showWarningMessage(
        resetUnavailableWarning,
        resetUnavailableAction,
      );
      return action === resetUnavailableAction;
    }

    handleError("reset", err);
    return false;
  }
}
