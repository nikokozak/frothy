package cmd

import "fmt"

var frothErrorNames = map[int]string{
	1:   "stack overflow",
	2:   "stack underflow",
	3:   "type mismatch",
	4:   "undefined word",
	5:   "division by zero",
	6:   "heap out of memory",
	7:   "invalid pattern",
	8:   "pattern too large",
	9:   "i/o error",
	10:  "throw with no catch",
	11:  "loop: stack discipline violation",
	12:  "value overflow",
	13:  "index out of bounds",
	14:  "interrupted",
	15:  "unbalanced return stack",
	16:  "slot table full",
	17:  "cannot redefine primitive",
	18:  "call depth exceeded",
	19:  "no mark set",
	21:  "too many FFI tables registered",
	22:  "transient string expired",
	23:  "transient string buffer full",
	24:  "cellspace full",
	25:  "defining word is top-level only",
	100: "token too long",
	101: "unterminated quotation",
	102: "unterminated comment",
	103: "unexpected )",
	104: "string too long",
	105: "unterminated string",
	106: "invalid escape sequence",
	107: "unexpected ]",
	108: "signature or arity metadata error",
	109: "named frame discipline error",
	200: "snapshot buffer overflow",
	201: "snapshot format error",
	202: "snapshot unresolved reference",
	203: "snapshot CRC mismatch",
	204: "snapshot incompatible ABI",
	205: "no saved snapshot",
	206: "snapshot name too long",
	207: "cannot save transient string",
	250: "link buffer overflow",
	251: "link COBS decode error",
	252: "link bad magic",
	253: "link bad version",
	254: "link CRC mismatch",
	255: "link payload too large",
	256: "link unknown message type",
}

func formatEvalError(code int, faultWord string) string {
	msg := fmt.Sprintf("error(%d)", code)
	if name, ok := frothErrorNames[code]; ok && name != "" {
		msg += ": " + name
	}
	if faultWord != "" {
		msg += fmt.Sprintf(" in %q", faultWord)
	}
	return msg
}
