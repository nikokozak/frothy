package cmd

import (
	"fmt"

	"github.com/nikokozak/frothy/tools/cli/internal/frothycontrol"
)

func runInfo() error {
	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: portFlag,
	})
	info, err := manager.Connect("")
	if err != nil {
		return err
	}
	defer manager.Disconnect()

	fmt.Printf("Frothy %s on %s\n", info.Version, info.Board)
	fmt.Printf("%d-bit cells, max payload %d\n", info.CellBits, info.MaxPayload)
	fmt.Printf("heap: %d / %d bytes\n", info.HeapUsed, info.HeapSize)
	fmt.Printf("slots: %d\n", info.SlotCount)
	return nil
}
