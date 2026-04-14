import { ControlSessionClientError } from "./control-session-client";

export const resetLogLine = "[frothy] reset";
export const resetUnavailableLogLine =
  "[frothy] whole-file Send File aborted: control reset unavailable on connected firmware";
export const resetUnavailableError =
  "Whole-file Send File requires control reset. The connected Frothy firmware is too old for safe whole-file send. Upgrade or reflash the firmware, or use Send Selection / Line for intentional additive eval.";

type ResetClient = {
  reset(): Promise<unknown>;
};

type ResetOutput = {
  appendLine(line: string): void;
};

type MessageFn =
  (message: string) => PromiseLike<string | undefined> | string | undefined;

type ErrorFn = (label: string, err: unknown) => void;

export async function prepareSendFileReset(
  client: ResetClient,
  output: ResetOutput,
  showErrorMessage: MessageFn,
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
      await showErrorMessage(resetUnavailableError);
      return false;
    }

    handleError("reset", err);
    return false;
  }
}
