type RuntimeFormState = {
  parenDepth: number;
  braceDepth: number;
  bracketDepth: number;
  inString: boolean;
  sawTopLevelIf: boolean;
  trailingEqual: boolean;
  trailingKeyword: boolean;
  trailingComma: boolean;
  trailingOperator: boolean;
  trailingHeader: boolean;
};

export function splitTopLevelForms(source: string): string[] {
  return collectTopLevelForms(source).map((form) => form.text);
}

export function findTopLevelFormAtLine(
  source: string,
  line: number,
): string | null {
  const lines = source.split("\n");
  let current = "";
  let state = makeRuntimeFormState();
  let hasCode = false;
  let startLine = -1;

  for (let index = 0; index < lines.length; index++) {
    const currentLine = lines[index];
    if (isLineComment(currentLine)) {
      continue;
    }
    if (!hasCode && currentLine.trim().length > 0) {
      startLine = index;
    }
    current += `${currentLine}\n`;
    scanChunk(state, currentLine);
    updateTrailingState(state, current);
    if (currentLine.trim().length > 0) {
      hasCode = true;
    }
    if (hasCode && runtimeFormComplete(state) && !continuesWithElse(lines, index + 1, state)) {
      if (line >= startLine && line <= index) {
        const form = current.trim();
        return form.length > 0 ? form : null;
      }
      current = "";
      state = makeRuntimeFormState();
      hasCode = false;
      startLine = -1;
    }
  }

  if (hasCode && line >= startLine) {
    throw new Error("incomplete Frothy source form");
  }
  return null;
}

function collectTopLevelForms(source: string): RuntimeFormSpan[] {
  const lines = source.split("\n");
  const forms: RuntimeFormSpan[] = [];
  let current = "";
  let state = makeRuntimeFormState();
  let hasCode = false;
  let startLine = -1;

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    if (isLineComment(line)) {
      continue;
    }
    if (!hasCode && line.trim().length > 0) {
      startLine = index;
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
        forms.push({
          text: form,
          startLine,
          endLine: index,
        });
      }
      current = "";
      state = makeRuntimeFormState();
      hasCode = false;
      startLine = -1;
    }
  }

  if (hasCode || current.trim().length > 0) {
    throw new Error("incomplete Frothy source form");
  }
  return forms;
}

type RuntimeFormSpan = {
  text: string;
  startLine: number;
  endLine: number;
};

function makeRuntimeFormState(): RuntimeFormState {
  return {
    parenDepth: 0,
    braceDepth: 0,
    bracketDepth: 0,
    inString: false,
    sawTopLevelIf: false,
    trailingEqual: false,
    trailingKeyword: false,
    trailingComma: false,
    trailingOperator: false,
    trailingHeader: false,
  };
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
  const keywordWords = new Set(["as", "else", "is", "to", "with"]);
  const operatorWords = new Set(["and", "not", "or"]);

  state.trailingEqual = false;
  state.trailingKeyword = false;
  state.trailingComma = false;
  state.trailingOperator = false;
  state.trailingHeader = false;
  if (state.inString) {
    return;
  }

  const i = trimEndIndex(source, source.length);
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
  if (i >= 2) {
    const prev = source[i - 2];
    if ((prev === "<" || prev === ">" || prev === "!" || prev === "=") && last === "=") {
      state.trailingOperator = true;
      return;
    }
  }
  if ("+-*/%<>".includes(last)) {
    state.trailingOperator = true;
    return;
  }

  let cursor = i;
  const prevWord = findPrevWord(source, cursor);
  if (prevWord) {
    cursor = prevWord.start;
    if (keywordWords.has(prevWord.word)) {
      state.trailingKeyword = true;
      return;
    }
    if (operatorWords.has(prevWord.word)) {
      state.trailingOperator = true;
      return;
    }
  }

  if (
    endsWithNamedCodeHeader(source, i) ||
    endsWithFunctionLiteralHeader(source, i) ||
    endsWithBlockHeader(source, i) ||
    endsWithCallHeader(source, i)
  ) {
    state.trailingHeader = true;
  }
}

function isNameContinue(ch: string): boolean {
  return /[A-Za-z0-9_.!?@]/.test(ch);
}

function isNameStart(ch: string): boolean {
  return /[A-Za-z_]/.test(ch);
}

function runtimeFormComplete(state: RuntimeFormState): boolean {
  return state.parenDepth === 0 &&
    state.braceDepth === 0 &&
    state.bracketDepth === 0 &&
    !state.inString &&
    !state.trailingEqual &&
    !state.trailingKeyword &&
    !state.trailingComma &&
    !state.trailingOperator &&
    !state.trailingHeader;
}

function trimEndIndex(text: string, end: number): number {
  while (end > 0 && /\s/.test(text[end - 1])) {
    end--;
  }
  return end;
}

function findPrevWord(
  text: string,
  cursor: number,
): { start: number; end: number; word: string } | null {
  const end = trimEndIndex(text, cursor);
  let start = end;
  while (start > 0 && isNameContinue(text[start - 1])) {
    start--;
  }
  if (start === end || !isNameStart(text[start])) {
    return null;
  }
  return {
    start,
    end,
    word: text.slice(start, end),
  };
}

function skipSpaces(text: string, start: number, end: number): number {
  while (start < end && /\s/.test(text[start])) {
    start++;
  }
  return start;
}

function segmentStart(text: string, end: number): number {
  while (end > 0) {
    if (text[end - 1] === ";") {
      return end;
    }
    end--;
  }
  return 0;
}

function findMatchingBracket(
  text: string,
  start: number,
  end: number,
  open: string,
  close: string,
): number {
  let depth = 0;
  let inString = false;

  for (let i = start; i < end; i++) {
    const ch = text[i];
    if (inString) {
      if (ch === "\\" && i + 1 < end) {
        i++;
        continue;
      }
      if (ch === "\"") {
        inString = false;
      }
      continue;
    }
    if (ch === "\"") {
      inString = true;
      continue;
    }
    if (ch === open) {
      depth++;
      continue;
    }
    if (ch === close) {
      depth--;
      if (depth === 0) {
        return i;
      }
    }
  }

  return -1;
}

function containsLeadingWord(
  text: string,
  start: number,
  end: number,
  word: string,
): boolean {
  start = skipSpaces(text, start, end);
  if (start >= end || !isNameStart(text[start])) {
    return false;
  }

  let tokenEnd = start + 1;
  while (tokenEnd < end && isNameContinue(text[tokenEnd])) {
    tokenEnd++;
  }
  return text.slice(start, tokenEnd) === word;
}

function findLastWord(
  text: string,
  start: number,
  end: number,
  word: string,
): number {
  let last = -1;

  for (let i = start; i < end; i++) {
    if (!isNameStart(text[i])) {
      continue;
    }
    const tokenStart = i;
    i++;
    while (i < end && isNameContinue(text[i])) {
      i++;
    }
    if (text.slice(tokenStart, i) === word) {
      last = tokenStart;
    }
  }

  return last;
}

function containsCallSeparator(text: string, start: number, end: number): boolean {
  let prevWord = "";

  for (let i = start; i < end; i++) {
    if (!isNameStart(text[i])) {
      continue;
    }
    const tokenStart = i;
    i++;
    while (i < end && isNameContinue(text[i])) {
      i++;
    }
    const word = text.slice(tokenStart, i);
    if (word === "with" && prevWord !== "fn") {
      return true;
    }
    prevWord = word;
  }

  return false;
}

function hasBodyOpener(text: string, start: number, end: number): boolean {
  for (let i = start; i < end; i++) {
    if (text[i] !== "[" && text[i] !== "{") {
      continue;
    }
    if (i !== start && !/\s/.test(text[i - 1])) {
      continue;
    }

    const open = text[i];
    const close = open === "{" ? "}" : "]";
    const match = findMatchingBracket(text, i, end, open, close);
    if (match < 0) {
      continue;
    }
    const tail = skipSpaces(text, match + 1, end);
    if (tail === end || containsCallSeparator(text, tail, end)) {
      return true;
    }
    if (containsLeadingWord(text, tail, end, "else")) {
      return true;
    }
  }

  return false;
}

function endsWithNamedCodeHeader(text: string, end: number): boolean {
  let cursor = trimEndIndex(text, end);
  let word = findPrevWord(text, cursor);
  if (!word) {
    return false;
  }
  cursor = word.start;
  if (word.word === "to") {
    return true;
  }

  for (;;) {
    const before = trimEndIndex(text, cursor);
    if (before === 0 || text[before - 1] !== ",") {
      break;
    }
    cursor = before - 1;
    word = findPrevWord(text, cursor);
    if (!word) {
      return false;
    }
    cursor = word.start;
  }

  word = findPrevWord(text, cursor);
  if (!word) {
    return false;
  }
  cursor = word.start;
  if (word.word === "to") {
    return true;
  }
  if (word.word !== "with") {
    return false;
  }

  word = findPrevWord(text, cursor);
  if (!word) {
    return false;
  }
  cursor = word.start;
  word = findPrevWord(text, cursor);
  return word?.word === "to";
}

function endsWithFunctionLiteralHeader(text: string, end: number): boolean {
  let cursor = trimEndIndex(text, end);
  if (cursor === 0) {
    return false;
  }
  const last = text[cursor - 1];
  if (last === "]" || last === "}") {
    return false;
  }

  let word = findPrevWord(text, cursor);
  if (!word) {
    return false;
  }
  cursor = word.start;
  if (word.word === "fn") {
    return true;
  }

  for (;;) {
    const before = trimEndIndex(text, cursor);
    if (before === 0 || text[before - 1] !== ",") {
      break;
    }
    cursor = before - 1;
    word = findPrevWord(text, cursor);
    if (!word) {
      return false;
    }
    cursor = word.start;
  }

  word = findPrevWord(text, cursor);
  if (!word || word.word !== "with") {
    return false;
  }
  cursor = word.start;
  word = findPrevWord(text, cursor);
  return word?.word === "fn";
}

function endsWithBlockHeader(text: string, end: number): boolean {
  end = trimEndIndex(text, end);
  if (end === 0) {
    return false;
  }
  const last = text[end - 1];
  if (last === "]" || last === "}") {
    return false;
  }

  const start = segmentStart(text, end);
  let cursor = skipSpaces(text, start, end);
  if (cursor >= end || !isNameStart(text[cursor])) {
    return false;
  }

  const wordStart = cursor;
  cursor++;
  while (cursor < end && isNameContinue(text[cursor])) {
    cursor++;
  }
  const word = text.slice(wordStart, cursor);
  if (!["fn", "cond", "case", "in", "if", "when", "unless", "repeat", "while"].includes(word)) {
    return false;
  }

  return !hasBodyOpener(text, cursor, end);
}

function endsWithCallHeader(text: string, end: number): boolean {
  end = trimEndIndex(text, end);
  const start = segmentStart(text, end);
  const callStart = findLastWord(text, start, end, "call");
  if (callStart < 0) {
    return false;
  }
  return !containsCallSeparator(text, callStart + "call".length, end);
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
