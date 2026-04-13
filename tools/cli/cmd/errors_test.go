package cmd

import "testing"

func TestFormatEvalErrorIncludesNameAndFaultWord(t *testing.T) {
	got := formatEvalError(4, "foo")
	want := `error(4): undefined word in "foo"`
	if got != want {
		t.Fatalf("formatEvalError() = %q, want %q", got, want)
	}
}

func TestFormatEvalErrorFallsBackToCode(t *testing.T) {
	got := formatEvalError(999, "")
	want := "error(999)"
	if got != want {
		t.Fatalf("formatEvalError() = %q, want %q", got, want)
	}
}
