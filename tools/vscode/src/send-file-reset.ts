import { ControlSessionClientError } from "./control-session-client";

export const resetLogLine = "[frothy] reset";
export const resetUnavailableLogLine =
  "[frothy] whole-file Send File aborted: control reset unavailable on connected firmware";
export const resetUnavailableProceedLogLine =
  "[frothy] whole-file Send File continuing without control reset; additive eval may leave stale bindings";
export const resetUnavailableError =
  "Whole-file Send File requires control reset. The connected Frothy firmware is too old for safe whole-file send. Upgrade or reflash the firmware, or use Send Selection / Line for intentional additive eval.";
export const resetUnavailableProceedAction = "Send Anyway";
export const resetUnavailableCancelAction = "Cancel";

export interface PrepareSendFileResetResult {
  proceed: boolean;
  degraded: boolean;
}

type ResetClient = {
  reset(): Promise<unknown>;
};

type ResetOutput = {
  appendLine(line: string): void;
};

type MessageFn = (
  message: string,
  ...items: string[]
) => PromiseLike<string | undefined> | string | undefined;

type ErrorFn = (label: string, err: unknown) => void;

export async function prepareSendFileReset(
  client: ResetClient,
  output: ResetOutput,
  showMessage: MessageFn,
  handleError: ErrorFn,
): Promise<PrepareSendFileResetResult> {
  try {
    await client.reset();
    output.appendLine(resetLogLine);
    return { proceed: true, degraded: false };
  } catch (err: unknown) {
    if (
      err instanceof ControlSessionClientError &&
      err.code === "reset_unavailable"
    ) {
      const action = await showMessage(
        resetUnavailableError,
        resetUnavailableProceedAction,
        resetUnavailableCancelAction,
      );
      if (action === resetUnavailableProceedAction) {
        output.appendLine(resetUnavailableProceedLogLine);
        return { proceed: true, degraded: true };
      }
      output.appendLine(resetUnavailableLogLine);
      return {
        proceed: false,
        degraded: false,
      };
    }

    handleError("reset", err);
    return { proceed: false, degraded: false };
  }
}
