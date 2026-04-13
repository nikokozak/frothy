package project

import (
	"reflect"
	"testing"
)

func TestSplitTopLevelFormsSingleExpr(t *testing.T) {
	forms, err := SplitTopLevelForms("1 + 2\n")
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	if !reflect.DeepEqual(forms, []string{"1 + 2"}) {
		t.Fatalf("forms = %#v", forms)
	}
}

func TestSplitTopLevelFormsMultipleForms(t *testing.T) {
	source := `note = nil

boot {
  set note = "Hello from Frothy!"
}
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"note = nil",
		"boot {\n  set note = \"Hello from Frothy!\"\n}",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsIgnoresLineComments(t *testing.T) {
	source := `\ comment
helper() = "LIB"
\ another
boot {
  \ inside block comment
  helper()
}
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		`helper() = "LIB"`,
		"boot {\n  helper()\n}",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsRejectsIncompleteInput(t *testing.T) {
	if _, err := SplitTopLevelForms("boot {\n"); err == nil {
		t.Fatal("SplitTopLevelForms succeeded, want error")
	}
}
