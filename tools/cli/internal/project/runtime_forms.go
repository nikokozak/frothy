package project

import (
	"fmt"
	"strings"
	"unicode"
)

type runtimeFormState struct {
	parenDepth       int
	braceDepth       int
	bracketDepth     int
	inString         bool
	sawTopLevelIf    bool
	trailingEqual    bool
	trailingKeyword  bool
	trailingComma    bool
	trailingOperator bool
	trailingHeader   bool
}

// SplitTopLevelForms splits runtime-ready Frothy source into individual
// top-level forms for control-surface evaluation. The device control endpoint
// accepts one top-level form per eval request, so multi-form project source
// must be streamed as a sequence of complete forms.
func SplitTopLevelForms(source string) ([]string, error) {
	lines := strings.Split(source, "\n")
	var (
		forms   []string
		current strings.Builder
		state   runtimeFormState
		hasCode bool
	)

	for index, line := range lines {
		if isLineComment(line) {
			continue
		}
		if err := runtimeFormAppendLine(&state, &current, line); err != nil {
			return nil, err
		}
		if strings.TrimSpace(line) != "" {
			hasCode = true
		}
		if hasCode && runtimeFormComplete(&state) && !runtimeFormContinuesWithElse(lines, index+1, &state) {
			form := strings.TrimSpace(current.String())
			if form != "" {
				forms = append(forms, form)
			}
			current.Reset()
			state = runtimeFormState{}
			hasCode = false
		}
	}

	if hasCode || strings.TrimSpace(current.String()) != "" {
		return nil, fmt.Errorf("incomplete Frothy source form")
	}
	return forms, nil
}

func isLineComment(line string) bool {
	trimmed := strings.TrimLeftFunc(line, unicode.IsSpace)
	return strings.HasPrefix(trimmed, `\`)
}

func runtimeFormAppendLine(state *runtimeFormState, current *strings.Builder, line string) error {
	current.WriteString(line)
	current.WriteByte('\n')
	runtimeFormScanChunk(state, line)
	runtimeFormUpdateTrailingState(state, current.String())
	return nil
}

func runtimeFormScanChunk(state *runtimeFormState, chunk string) {
	token := strings.Builder{}
	topLevel := state.parenDepth == 0 && state.braceDepth == 0 && state.bracketDepth == 0

	for i := 0; i < len(chunk); i++ {
		ch := chunk[i]
		if state.inString {
			if ch == '\\' && i+1 < len(chunk) {
				i++
				continue
			}
			if ch == '"' {
				state.inString = false
			}
			continue
		}

		if topLevel && runtimeFormIsNameContinue(ch) {
			token.WriteByte(ch)
		} else if token.Len() > 0 {
			if token.String() == "if" {
				state.sawTopLevelIf = true
			}
			token.Reset()
		}

		switch ch {
		case '"':
			state.inString = true
		case '(':
			state.parenDepth++
		case ')':
			if state.parenDepth > 0 {
				state.parenDepth--
			}
		case '[':
			state.bracketDepth++
		case ']':
			if state.bracketDepth > 0 {
				state.bracketDepth--
			}
		case '{':
			state.braceDepth++
		case '}':
			if state.braceDepth > 0 {
				state.braceDepth--
			}
		}

		topLevel = state.parenDepth == 0 && state.braceDepth == 0 && state.bracketDepth == 0
	}

	if topLevel && token.String() == "if" {
		state.sawTopLevelIf = true
	}
}

func runtimeFormUpdateTrailingState(state *runtimeFormState, source string) {
	keywordWords := []string{"as", "else", "is", "to", "with"}
	operatorWords := []string{"and", "not", "or"}

	state.trailingEqual = false
	state.trailingKeyword = false
	state.trailingComma = false
	state.trailingOperator = false
	state.trailingHeader = false
	if state.inString {
		return
	}

	i := len(source)
	for i > 0 && unicode.IsSpace(rune(source[i-1])) {
		i--
	}
	if i == 0 {
		return
	}

	last := source[i-1]
	if last == '=' {
		if i >= 2 {
			prev := source[i-2]
			if prev == '=' || prev == '!' || prev == '<' || prev == '>' {
				state.trailingOperator = true
				return
			}
		}
		state.trailingEqual = true
		return
	}
	if last == ',' {
		state.trailingComma = true
		return
	}
	if i >= 2 {
		prev := source[i-2]
		if (prev == '<' || prev == '>' || prev == '!' || prev == '=') && last == '=' {
			state.trailingOperator = true
			return
		}
	}
	switch last {
	case '+', '-', '*', '/', '%', '<', '>':
		state.trailingOperator = true
		return
	}

	cursor := i
	if start, end, ok := runtimeFormFindPrevWord(source, &cursor); ok {
		word := source[start:end]
		if runtimeFormWordInList(word, keywordWords) {
			state.trailingKeyword = true
			return
		}
		if runtimeFormWordInList(word, operatorWords) {
			state.trailingOperator = true
			return
		}
	}

	if runtimeFormEndsWithNamedCodeHeader(source, i) ||
		runtimeFormEndsWithFunctionLiteralHeader(source, i) ||
		runtimeFormEndsWithBlockHeader(source, i) ||
		runtimeFormEndsWithCallHeader(source, i) {
		state.trailingHeader = true
	}
}

func runtimeFormIsNameContinue(ch byte) bool {
	return ch == '_' || ch == '.' ||
		(ch >= 'a' && ch <= 'z') ||
		(ch >= 'A' && ch <= 'Z') ||
		(ch >= '0' && ch <= '9')
}

func runtimeFormComplete(state *runtimeFormState) bool {
	return state.parenDepth == 0 &&
		state.braceDepth == 0 &&
		state.bracketDepth == 0 &&
		!state.inString &&
		!state.trailingEqual &&
		!state.trailingKeyword &&
		!state.trailingComma &&
		!state.trailingOperator &&
		!state.trailingHeader
}

func runtimeFormTrimEndIndex(text string, end int) int {
	for end > 0 && unicode.IsSpace(rune(text[end-1])) {
		end--
	}
	return end
}

func runtimeFormIsNameStart(ch byte) bool {
	return ch == '_' ||
		(ch >= 'a' && ch <= 'z') ||
		(ch >= 'A' && ch <= 'Z')
}

func runtimeFormFindPrevWord(text string, cursor *int) (int, int, bool) {
	end := runtimeFormTrimEndIndex(text, *cursor)
	start := end
	for start > 0 && runtimeFormIsNameContinue(text[start-1]) {
		start--
	}
	if start == end || !runtimeFormIsNameStart(text[start]) {
		return 0, 0, false
	}
	*cursor = start
	return start, end, true
}

func runtimeFormWordInList(word string, words []string) bool {
	for _, candidate := range words {
		if word == candidate {
			return true
		}
	}
	return false
}

func runtimeFormSkipSpaces(text string, start, end int) int {
	for start < end && unicode.IsSpace(rune(text[start])) {
		start++
	}
	return start
}

func runtimeFormSegmentStart(text string, end int) int {
	for end > 0 {
		if text[end-1] == ';' {
			return end
		}
		end--
	}
	return 0
}

func runtimeFormFindMatchingBracket(text string, start, end int, open, close byte) int {
	depth := 0
	inString := false

	for i := start; i < end; i++ {
		ch := text[i]
		if inString {
			if ch == '\\' && i+1 < end {
				i++
				continue
			}
			if ch == '"' {
				inString = false
			}
			continue
		}
		if ch == '"' {
			inString = true
			continue
		}
		if ch == open {
			depth++
			continue
		}
		if ch == close {
			depth--
			if depth == 0 {
				return i
			}
		}
	}

	return -1
}

func runtimeFormHasBodyOpener(text string, start, end int) bool {
	for i := start; i < end; i++ {
		if text[i] != '[' && text[i] != '{' {
			continue
		}
		if i == start || unicode.IsSpace(rune(text[i-1])) {
			open := text[i]
			close := byte(']')
			if open == '{' {
				close = '}'
			}
			match := runtimeFormFindMatchingBracket(text, i, end, open, close)
			if match < 0 {
				continue
			}
			tail := runtimeFormSkipSpaces(text, match+1, end)
			if tail == end || runtimeFormContainsCallSeparator(text, tail, end) {
				return true
			}
			if runtimeFormContainsLeadingWord(text, tail, end, "else") {
				return true
			}
		}
	}
	return false
}

func runtimeFormContainsLeadingWord(text string, start, end int, word string) bool {
	start = runtimeFormSkipSpaces(text, start, end)
	if start >= end || !runtimeFormIsNameStart(text[start]) {
		return false
	}
	tokenEnd := start + 1
	for tokenEnd < end && runtimeFormIsNameContinue(text[tokenEnd]) {
		tokenEnd++
	}
	return text[start:tokenEnd] == word
}

func runtimeFormFindLastWord(text string, start, end int, word string) int {
	last := -1

	for i := start; i < end; i++ {
		if !runtimeFormIsNameStart(text[i]) {
			continue
		}
		tokenStart := i
		i++
		for i < end && runtimeFormIsNameContinue(text[i]) {
			i++
		}
		if text[tokenStart:i] == word {
			last = tokenStart
		}
	}

	return last
}

func runtimeFormContainsCallSeparator(text string, start, end int) bool {
	prevWord := ""

	for i := start; i < end; i++ {
		if !runtimeFormIsNameStart(text[i]) {
			continue
		}
		tokenStart := i
		i++
		for i < end && runtimeFormIsNameContinue(text[i]) {
			i++
		}
		word := text[tokenStart:i]
		if word == "with" && prevWord != "fn" {
			return true
		}
		prevWord = word
	}
	return false
}

func runtimeFormEndsWithNamedCodeHeader(text string, end int) bool {
	cursor := runtimeFormTrimEndIndex(text, end)
	start, finish, ok := runtimeFormFindPrevWord(text, &cursor)
	if !ok {
		return false
	}
	if text[start:finish] == "to" {
		return true
	}

	for {
		before := runtimeFormTrimEndIndex(text, cursor)
		if before == 0 || text[before-1] != ',' {
			break
		}
		cursor = before - 1
		if _, _, ok := runtimeFormFindPrevWord(text, &cursor); !ok {
			return false
		}
	}

	start, finish, ok = runtimeFormFindPrevWord(text, &cursor)
	if !ok {
		return false
	}
	word := text[start:finish]
	if word == "to" {
		return true
	}
	if word != "with" {
		return false
	}
	if _, _, ok = runtimeFormFindPrevWord(text, &cursor); !ok {
		return false
	}
	start, finish, ok = runtimeFormFindPrevWord(text, &cursor)
	if !ok {
		return false
	}
	return text[start:finish] == "to"
}

func runtimeFormEndsWithFunctionLiteralHeader(text string, end int) bool {
	cursor := runtimeFormTrimEndIndex(text, end)
	if cursor == 0 {
		return false
	}
	last := text[cursor-1]
	if last == ']' || last == '}' {
		return false
	}

	start, finish, ok := runtimeFormFindPrevWord(text, &cursor)
	if !ok {
		return false
	}
	if text[start:finish] == "fn" {
		return true
	}

	for {
		before := runtimeFormTrimEndIndex(text, cursor)
		if before == 0 || text[before-1] != ',' {
			break
		}
		cursor = before - 1
		if _, _, ok := runtimeFormFindPrevWord(text, &cursor); !ok {
			return false
		}
	}

	start, finish, ok = runtimeFormFindPrevWord(text, &cursor)
	if !ok || text[start:finish] != "with" {
		return false
	}
	start, finish, ok = runtimeFormFindPrevWord(text, &cursor)
	if !ok {
		return false
	}
	return text[start:finish] == "fn"
}

func runtimeFormEndsWithBlockHeader(text string, end int) bool {
	end = runtimeFormTrimEndIndex(text, end)
	if end == 0 {
		return false
	}
	last := text[end-1]
	if last == ']' || last == '}' {
		return false
	}

	segment := runtimeFormSegmentStart(text, end)
	cursor := runtimeFormSkipSpaces(text, segment, end)
	if cursor >= end || !runtimeFormIsNameStart(text[cursor]) {
		return false
	}

	wordStart := cursor
	cursor++
	for cursor < end && runtimeFormIsNameContinue(text[cursor]) {
		cursor++
	}
	word := text[wordStart:cursor]
	if !runtimeFormWordInList(word, []string{
		"fn", "cond", "case", "in", "if", "when", "unless", "repeat", "while",
	}) {
		return false
	}

	return !runtimeFormHasBodyOpener(text, cursor, end)
}

func runtimeFormEndsWithCallHeader(text string, end int) bool {
	end = runtimeFormTrimEndIndex(text, end)
	segment := runtimeFormSegmentStart(text, end)
	callStart := runtimeFormFindLastWord(text, segment, end, "call")
	if callStart < 0 {
		return false
	}
	return !runtimeFormContainsCallSeparator(text, callStart+len("call"), end)
}

func runtimeFormContinuesWithElse(lines []string, start int, state *runtimeFormState) bool {
	if !state.sawTopLevelIf {
		return false
	}
	for index := start; index < len(lines); index++ {
		trimmed := strings.TrimSpace(lines[index])
		if trimmed == "" || strings.HasPrefix(trimmed, `\`) {
			continue
		}
		return runtimeFormIsElseContinuation(trimmed)
	}
	return false
}

func runtimeFormIsElseContinuation(trimmed string) bool {
	if !strings.HasPrefix(trimmed, "else") {
		return false
	}
	if len(trimmed) == 4 {
		return true
	}
	return !runtimeFormIsNameContinue(trimmed[4])
}
