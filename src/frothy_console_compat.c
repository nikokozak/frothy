#include "frothy_shell.h"

/*
 * The inherited live-console path still asks the active language shell whether
 * it is idle. Report the Frothy shell's primary-prompt state so attach gating
 * remains truthful during boot, multiline input, and active evaluation.
 */
int froth_repl_is_idle(void) { return frothy_shell_is_idle() ? 1 : 0; }
