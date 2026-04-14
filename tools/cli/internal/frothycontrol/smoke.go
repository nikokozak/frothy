package frothycontrol

import (
	"bytes"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/serial"
)

type SmokeConfig struct {
	Port         string
	LocalRuntime string
}

func runRawMultilineInterrupt(session *Session) error {
	if err := session.writeBytes([]byte("control.check = fn(x) {\n")); err != nil {
		return fmt.Errorf("send multiline source: %w", err)
	}
	if _, err := session.waitForRaw([]byte(".. "), rawPromptTimeout); err != nil {
		return fmt.Errorf("wait for continuation prompt: %w", err)
	}
	if err := session.Interrupt(); err != nil {
		return fmt.Errorf("interrupt multiline input: %w", err)
	}
	if _, err := session.waitForRaw([]byte("frothy> "), rawPromptTimeout); err != nil {
		return fmt.Errorf("wait for prompt after multiline interrupt: %w", err)
	}
	return nil
}

func enterControlAndHello(session *Session) error {
	if err := session.EnterControl(rawPromptTimeout); err != nil {
		return fmt.Errorf("enter control: %w", err)
	}

	hello, err := session.Hello(controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("HELLO: %w", err)
	}
	if hello.Version == "" || hello.Board == "" {
		return fmt.Errorf("HELLO missing version or board")
	}
	return nil
}

func interruptIdleControlSession(session *Session) error {
	if err := session.Interrupt(); err != nil {
		return fmt.Errorf("interrupt idle control session: %w", err)
	}
	if _, err := session.waitForRaw([]byte("frothy> "), rawPromptTimeout); err != nil {
		return fmt.Errorf("wait for prompt after idle control interrupt: %w", err)
	}
	session.sessionID = 0
	session.nextSeq = 0
	return nil
}

func RunSmoke(cfg SmokeConfig) error {
	var (
		transport serial.Transport
		runtime   *LocalRuntime
		err       error
	)

	if cfg.LocalRuntime != "" {
		runtime, err = StartLocalRuntime(cfg.LocalRuntime)
		if err != nil {
			return err
		}
		defer runtime.Close()
		transport = runtime.Transport()
	} else {
		transport, err = OpenSerial(cfg.Port)
		if err != nil {
			return err
		}
		defer func() {
			if transport != nil {
				_ = transport.Close()
			}
		}()
	}

	session := NewSession(transport)
	var controlErr *ControlError
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		return fmt.Errorf("acquire prompt: %w", err)
	}
	if err := runRawMultilineInterrupt(session); err != nil {
		return fmt.Errorf("multiline interrupt recovery: %w", err)
	}
	if err := enterControlAndHello(session); err != nil {
		return err
	}
	if err := interruptIdleControlSession(session); err != nil {
		return fmt.Errorf("idle control interrupt recovery: %w", err)
	}
	if err := enterControlAndHello(session); err != nil {
		return fmt.Errorf("re-enter control after interrupt: %w", err)
	}

	value, err := session.Eval("control.demo = 42", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("EVAL control.demo = 42: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected definition value %q", value)
	}

	var output bytes.Buffer
	value, err = session.Core("save", controlCommandTimeout,
		func(data []byte) {
			output.Write(data)
		})
	if err != nil {
		return fmt.Errorf("CORE save: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected output-producing value %q", value)
	}
	if !strings.Contains(output.String(), "<native save/0>") {
		return fmt.Errorf("missing structured output: %q", output.String())
	}

	output.Reset()
	value, err = session.SlotInfo("save", controlCommandTimeout, func(data []byte) {
		output.Write(data)
	})
	if err != nil {
		return fmt.Errorf("SLOT_INFO save: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected slot info value %q", value)
	}
	if !strings.Contains(output.String(),
		"save | base | native | non-persistable | foreign") {
		return fmt.Errorf("missing slot info output: %q", output.String())
	}

	words, err := session.Words(controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("WORDS: %w", err)
	}
	if !contains(words, "control.demo") {
		return fmt.Errorf("WORDS missing control.demo")
	}
	if len(words) < 2 {
		return fmt.Errorf("WORDS returned too few bindings: %d", len(words))
	}

	see, err := session.See("control.demo", controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("SEE control.demo: %w", err)
	}
	if see.Name != "control.demo" || see.Rendered != "42" || !see.IsOverlay {
		return fmt.Errorf("unexpected SEE result: %+v", see)
	}

	value, err = session.Save(controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("SAVE: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected SAVE value %q", value)
	}
	value, err = session.Eval("control.demo = 7", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("mutate after save: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected mutation value %q", value)
	}
	value, err = session.Restore(controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("RESTORE: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected RESTORE value %q", value)
	}
	see, err = session.See("control.demo", controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("SEE control.demo after restore: %w", err)
	}
	if see.Rendered != "42" {
		return fmt.Errorf("unexpected restored SEE render %q", see.Rendered)
	}
	value, err = session.Wipe(controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("WIPE: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected WIPE value %q", value)
	}
	_, err = session.See("control.demo", controlCommandTimeout)
	if !errors.As(err, &controlErr) || controlErr.Phase != phaseInspect {
		return fmt.Errorf("SEE control.demo after wipe: %v", err)
	}
	value, err = session.Eval("control.demo = 42", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("reseed control.demo after wipe: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected reseed value %q", value)
	}

	bigText := strings.Repeat("a", 240)
	for i := 0; i < 32; i++ {
		source := fmt.Sprintf("chunk.word.%02d = %d", i, i)
		value, err = session.Eval(source, controlCommandTimeout, nil)
		if err != nil {
			return fmt.Errorf("seed %s: %w", source, err)
		}
		if value != "nil" {
			return fmt.Errorf("unexpected seed value %q", value)
		}
	}
	value, err = session.Eval(fmt.Sprintf("bigText = %q", bigText),
		controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("seed chunked inspect state: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected seed value %q", value)
	}

	words, err = session.Words(controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("chunked WORDS: %w", err)
	}
	if !contains(words, "chunk.word.00") || !contains(words, "chunk.word.31") {
		return fmt.Errorf("chunked WORDS missing seeded bindings")
	}

	see, err = session.See("bigText", controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("SEE bigText: %w", err)
	}
	if see.Rendered != fmt.Sprintf("%q", bigText) {
		return fmt.Errorf("unexpected chunked SEE render %q", see.Rendered)
	}

	reset, err := session.Reset(controlCommandTimeout)
	if err != nil {
		return fmt.Errorf("RESET: %w", err)
	}
	if reset.Status != 0 || reset.HeapOverlayUsed != 0 || reset.SlotOverlayCount != 0 {
		return fmt.Errorf("unexpected RESET result: %+v", reset)
	}

	_, err = session.See("control.demo", controlCommandTimeout)
	if !errors.As(err, &controlErr) || controlErr.Phase != phaseInspect {
		return fmt.Errorf("SEE control.demo after reset: %v", err)
	}

	value, err = session.Eval("after.reset = 7", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("post-reset EVAL after.reset = 7: %w", err)
	}
	if value != "nil" {
		return fmt.Errorf("unexpected post-reset definition value %q", value)
	}

	value, err = session.Eval("after.reset", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("post-reset EVAL after.reset: %w", err)
	}
	if value != "7" {
		return fmt.Errorf("unexpected post-reset lookup value %q", value)
	}

	interruptOutput := make(chan struct{}, 1)
	interruptErr := make(chan error, 1)
	var interruptTranscript bytes.Buffer
	go func() {
		_, err := session.Eval(`while true { core("save") }`, 0,
			func(data []byte) {
				if len(data) == 0 {
					return
				}
				interruptTranscript.Write(data)
				select {
				case interruptOutput <- struct{}{}:
				default:
				}
			})
		if errors.Is(err, ErrInterrupted) {
			if !strings.Contains(interruptTranscript.String(), "<native save/0>") {
				interruptErr <- fmt.Errorf("missing interrupt output: %q",
					interruptTranscript.String())
				return
			}
			interruptErr <- nil
			return
		}
		if err == nil && !strings.Contains(interruptTranscript.String(), "<native save/0>") {
			interruptErr <- fmt.Errorf("missing interrupt output: %q",
				interruptTranscript.String())
			return
		}
		interruptErr <- err
	}()

	sawInterruptOutput := false
	for !sawInterruptOutput {
		select {
		case <-interruptOutput:
			sawInterruptOutput = true
		case err := <-interruptErr:
			if err != nil {
				return fmt.Errorf("interrupt eval before output: %w", err)
			}
			return fmt.Errorf("interrupt eval ended before producing output")
		case <-time.After(controlCommandTimeout):
			return fmt.Errorf("timed out waiting for interruptable output")
		}
	}
	if runtime != nil {
		if err := runtime.Interrupt(); err != nil {
			return err
		}
	} else {
		if err := session.Interrupt(); err != nil {
			return err
		}
	}
	select {
	case err := <-interruptErr:
		if err != nil {
			return fmt.Errorf("interrupt eval: %w", err)
		}
	case <-time.After(controlCommandTimeout):
		return fmt.Errorf("timed out waiting for interrupted eval to finish")
	}

	value, err = session.Eval("1 + 1", controlCommandTimeout, nil)
	if err != nil {
		return fmt.Errorf("post-interrupt EVAL 1 + 1: %w", err)
	}
	if value != "2" {
		return fmt.Errorf("post-interrupt eval returned %q", value)
	}

	if err := session.Detach(controlCommandTimeout); err != nil {
		return fmt.Errorf("DETACH: %w", err)
	}
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		return fmt.Errorf("post-detach prompt: %w", err)
	}

	if cfg.LocalRuntime == "" {
		if err := transport.Close(); err != nil {
			return fmt.Errorf("close before recovery: %w", err)
		}
		transport = nil
		if err := runDisconnectRecovery(cfg.Port); err != nil {
			return fmt.Errorf("disconnect recovery: %w", err)
		}
	}

	return nil
}

func runDisconnectRecovery(port string) error {
	transport, err := OpenSerial(port)
	if err != nil {
		return err
	}
	session := NewSession(transport)
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		_ = transport.Close()
		return err
	}
	if err := session.EnterControl(rawPromptTimeout); err != nil {
		_ = transport.Close()
		return err
	}
	if _, err := session.Hello(controlCommandTimeout); err != nil {
		_ = transport.Close()
		return err
	}
	if err := transport.Close(); err != nil {
		return err
	}

	transport, err = OpenSerial(port)
	if err != nil {
		return err
	}
	defer transport.Close()

	recoverSession := NewSession(transport)
	if err := recoverSession.AcquirePrompt(rawPromptTimeout); err != nil {
		return err
	}
	if err := recoverSession.EnterControl(rawPromptTimeout); err != nil {
		return err
	}
	if _, err := recoverSession.Hello(controlCommandTimeout); err != nil {
		return err
	}
	if err := recoverSession.Detach(controlCommandTimeout); err != nil {
		return err
	}
	return recoverSession.AcquirePrompt(rawPromptTimeout)
}

func contains(values []string, needle string) bool {
	for _, value := range values {
		if value == needle {
			return true
		}
	}
	return false
}
