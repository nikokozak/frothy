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
	source := `note is nil

to boot
[ set note to "Hello from Frothy!" ]
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"note is nil",
		"to boot\n[ set note to \"Hello from Frothy!\" ]",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsIgnoresLineComments(t *testing.T) {
	source := `\ comment
helper is "LIB"
\ another
to boot
[
  \ inside block comment
  helper
]
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		`helper is "LIB"`,
		"to boot\n[\n  helper\n]",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsHandlesSpokenContinuations(t *testing.T) {
	source := "count is\n9\n\ncall\nmakeInc:\nwith 41\n"
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"count is\n9",
		"call\nmakeInc:\nwith 41",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsHandlesMaintainedHeaderShapes(t *testing.T) {
	source := `when frame[0] == 1
[ 2 ]

when frame [0] == 1
[ 2 ]

repeat items[0] as
i
[ i ]

value is fn
with item
[ item ]

invoke is call fn with item
[ item ]
with 41
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"when frame[0] == 1\n[ 2 ]",
		"when frame [0] == 1\n[ 2 ]",
		"repeat items[0] as\ni\n[ i ]",
		"value is fn\nwith item\n[ item ]",
		"invoke is call fn with item\n[ item ]\nwith 41",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsRejectsIncompleteInput(t *testing.T) {
	if _, err := SplitTopLevelForms("to boot\n"); err == nil {
		t.Fatal("SplitTopLevelForms succeeded, want error")
	}
	if _, err := SplitTopLevelForms("call\nmakeInc:\n"); err == nil {
		t.Fatal("SplitTopLevelForms call header succeeded, want error")
	}
	if _, err := SplitTopLevelForms("call fn with item\n[ item ]\n"); err == nil {
		t.Fatal("SplitTopLevelForms wrapped fn call header succeeded, want error")
	}
	if _, err := SplitTopLevelForms("repeat items[0] as\ni\n"); err == nil {
		t.Fatal("SplitTopLevelForms repeat header succeeded, want error")
	}
}

func TestSplitTopLevelFormsKeepsIfElseTogether(t *testing.T) {
	source := `keep = if true {
  1
}
else {
  2
}
after = 7
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"keep = if true {\n  1\n}\nelse {\n  2\n}",
		"after = 7",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}

func TestSplitTopLevelFormsDoesNotTreatElsePrefixesAsElseBranch(t *testing.T) {
	source := `keep = if true {
  1
}
elsewhere = 2
`
	forms, err := SplitTopLevelForms(source)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	want := []string{
		"keep = if true {\n  1\n}",
		"elsewhere = 2",
	}
	if !reflect.DeepEqual(forms, want) {
		t.Fatalf("forms = %#v, want %#v", forms, want)
	}
}
