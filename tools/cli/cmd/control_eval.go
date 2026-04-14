package cmd

import (
	"fmt"
	"os"
	"os/signal"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/project"
)

type controlEvalManager interface {
	Eval(source string, onOutput func([]byte)) (string, error)
	Interrupt() error
}

func runControlEval(manager controlEvalManager, source string) (string, error) {
	sigintCh := make(chan os.Signal, 1)
	signal.Notify(sigintCh, os.Interrupt)
	defer signal.Stop(sigintCh)

	type evalOutcome struct {
		value string
		err   error
	}

	evalCh := make(chan evalOutcome, 1)
	go func() {
		value, err := manager.Eval(source, func(data []byte) {
			_, _ = os.Stdout.Write(data)
		})
		evalCh <- evalOutcome{value: value, err: err}
	}()

	for {
		select {
		case outcome := <-evalCh:
			return outcome.value, outcome.err
		case <-sigintCh:
			interruptErr := manager.Interrupt()
			if interruptErr != nil {
				fmt.Fprintf(os.Stderr, "interrupt: %v\n", interruptErr)
			}
		}
	}
}

func runControlSource(manager controlEvalManager, source string) (string, error) {
	forms, err := project.SplitTopLevelForms(source)
	if err != nil {
		return "", err
	}
	if len(forms) == 0 {
		return "", nil
	}

	lastValue := ""
	for _, form := range forms {
		lastValue, err = runControlEval(manager, form)
		if err != nil {
			return "", fmt.Errorf("eval %q: %w", previewControlForm(form), err)
		}
	}
	return lastValue, nil
}

func previewControlForm(source string) string {
	compact := strings.Join(strings.Fields(source), " ")
	if len(compact) <= 80 {
		return compact
	}
	return compact[:77] + "..."
}

func printControlValue(value string) {
	if value == "" || value == "nil" {
		return
	}
	fmt.Println(value)
}

func isInterrupted(err error) bool {
	return err == nil || err == frothycontrol.ErrInterrupted
}

func contains(values []string, needle string) bool {
	for _, value := range values {
		if value == needle {
			return true
		}
	}
	return false
}
