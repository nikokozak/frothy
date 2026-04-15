package cmd

import (
	"errors"
	"fmt"

	"github.com/nikokozak/frothy/tools/cli/internal/frothycontrol"
)

func runReset() error {
	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: portFlag,
	})
	if _, err := manager.Connect(""); err != nil {
		return err
	}
	defer manager.Disconnect()

	reset, err := manager.Reset()
	if err != nil {
		if errors.Is(err, frothycontrol.ErrResetUnavailable) {
			return fmt.Errorf("reset: %w", err)
		}
		return fmt.Errorf("reset: %w", err)
	}

	fmt.Printf("Reset result: OK\n")
	fmt.Printf("heap: %d / %d bytes\n", reset.HeapUsed, reset.HeapSize)
	fmt.Printf("slots: %d\n", reset.SlotCount)
	return nil
}
