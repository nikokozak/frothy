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
	trailingEqual    bool
	trailingComma    bool
	trailingOperator bool
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

	for _, line := range lines {
		if isLineComment(line) {
			continue
		}
		if err := runtimeFormAppendLine(&state, &current, line); err != nil {
			return nil, err
		}
		if strings.TrimSpace(line) != "" {
			hasCode = true
		}
		if hasCode && runtimeFormComplete(&state) {
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
	}
}

func runtimeFormUpdateTrailingState(state *runtimeFormState, source string) {
	state.trailingEqual = false
	state.trailingComma = false
	state.trailingOperator = false
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
	if i >= 3 {
		tokenEnd := i
		tokenStart := tokenEnd
		for tokenStart > 0 && runtimeFormIsNameContinue(source[tokenStart-1]) {
			tokenStart--
		}
		if tokenEnd-tokenStart == 3 && source[tokenStart:tokenEnd] == "not" &&
			(tokenStart == 0 || !runtimeFormIsNameContinue(source[tokenStart-1])) {
			state.trailingOperator = true
			return
		}
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
		!state.trailingComma &&
		!state.trailingOperator
}
