type RuntimeFormState = {
  parenDepth: number;
  braceDepth: number;
  bracketDepth: number;
  inString: boolean;
  sawTopLevelIf: boolean;
  trailingEqual: boolean;
  trailingComma: boolean;
  trailingOperator: boolean;
};

export function splitTopLevelForms(source: string): string[] {
  const lines = source.split("\n");
  const forms: string[] = [];
  let current = "";
  let state: RuntimeFormState = {
    parenDepth: 0,
    braceDepth: 0,
    bracketDepth: 0,
    inString: false,
    sawTopLevelIf: false,
    trailingEqual: false,
    trailingComma: false,
    trailingOperator: false,
  };
  let hasCode = false;

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    if (isLineComment(line)) {
      continue;
    }
    current += `${line}\n`;
    scanChunk(state, line);
    updateTrailingState(state, current);
    if (line.trim().length > 0) {
      hasCode = true;
    }
    if (hasCode && runtimeFormComplete(state) && !continuesWithElse(lines, index + 1, state)) {
      const form = current.trim();
      if (form.length > 0) {
        forms.push(form);
      }
      current = "";
      state = {
        parenDepth: 0,
        braceDepth: 0,
        bracketDepth: 0,
        inString: false,
        sawTopLevelIf: false,
        trailingEqual: false,
        trailingComma: false,
        trailingOperator: false,
      };
      hasCode = false;
    }
  }

  if (hasCode || current.trim().length > 0) {
    throw new Error("incomplete Frothy source form");
  }
  return forms;
}

function isLineComment(line: string): boolean {
  return line.trimStart().startsWith("\\");
}

function scanChunk(state: RuntimeFormState, chunk: string): void {
  let token = "";
  let topLevel = state.parenDepth === 0 &&
    state.braceDepth === 0 &&
    state.bracketDepth === 0;

  for (let i = 0; i < chunk.length; i++) {
    const ch = chunk[i];
    if (state.inString) {
      if (ch === "\\" && i + 1 < chunk.length) {
        i++;
        continue;
      }
      if (ch === "\"") {
        state.inString = false;
      }
      continue;
    }

    if (topLevel && isNameContinue(ch)) {
      token += ch;
    } else if (token.length > 0) {
      if (token === "if") {
        state.sawTopLevelIf = true;
      }
      token = "";
    }

    switch (ch) {
      case "\"":
        state.inString = true;
        break;
      case "(":
        state.parenDepth++;
        break;
      case ")":
        if (state.parenDepth > 0) {
          state.parenDepth--;
        }
        break;
      case "[":
        state.bracketDepth++;
        break;
      case "]":
        if (state.bracketDepth > 0) {
          state.bracketDepth--;
        }
        break;
      case "{":
        state.braceDepth++;
        break;
      case "}":
        if (state.braceDepth > 0) {
          state.braceDepth--;
        }
        break;
    }

    topLevel = state.parenDepth === 0 &&
      state.braceDepth === 0 &&
      state.bracketDepth === 0;
  }

  if (topLevel && token === "if") {
    state.sawTopLevelIf = true;
  }
}

function updateTrailingState(state: RuntimeFormState, source: string): void {
  state.trailingEqual = false;
  state.trailingComma = false;
  state.trailingOperator = false;
  if (state.inString) {
    return;
  }

  let i = source.length;
  while (i > 0 && /\s/.test(source[i - 1])) {
    i--;
  }
  if (i === 0) {
    return;
  }

  const last = source[i - 1];
  if (last === "=") {
    if (i >= 2) {
      const prev = source[i - 2];
      if (prev === "=" || prev === "!" || prev === "<" || prev === ">") {
        state.trailingOperator = true;
        return;
      }
    }
    state.trailingEqual = true;
    return;
  }
  if (last === ",") {
    state.trailingComma = true;
    return;
  }
  if (i >= 3) {
    const tokenEnd = i;
    let tokenStart = tokenEnd;
    while (tokenStart > 0 && isNameContinue(source[tokenStart - 1])) {
      tokenStart--;
    }
    if (
      tokenEnd - tokenStart === 3 &&
      source.slice(tokenStart, tokenEnd) === "not" &&
      (tokenStart === 0 || !isNameContinue(source[tokenStart - 1]))
    ) {
      state.trailingOperator = true;
      return;
    }
  }
  if (i >= 2) {
    const prev = source[i - 2];
    if ((prev === "<" || prev === ">" || prev === "!" || prev === "=") && last === "=") {
      state.trailingOperator = true;
      return;
    }
  }
  if ("+-*/%<>".includes(last)) {
    state.trailingOperator = true;
  }
}

function isNameContinue(ch: string): boolean {
  return /[A-Za-z0-9_.]/.test(ch);
}

function runtimeFormComplete(state: RuntimeFormState): boolean {
  return state.parenDepth === 0 &&
    state.braceDepth === 0 &&
    state.bracketDepth === 0 &&
    !state.inString &&
    !state.trailingEqual &&
    !state.trailingComma &&
    !state.trailingOperator;
}

function continuesWithElse(
  lines: string[],
  startIndex: number,
  state: RuntimeFormState,
): boolean {
  if (!state.sawTopLevelIf) {
    return false;
  }

  for (let index = startIndex; index < lines.length; index++) {
    const trimmed = lines[index].trimStart();
    if (trimmed.length === 0 || trimmed.startsWith("\\")) {
      continue;
    }
    return /^else(?:$|[^A-Za-z0-9_.])/.test(trimmed);
  }
  return false;
}
