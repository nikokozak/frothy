package frothycontrol

import "time"

const (
	rawPromptTimeout      = 12 * time.Second
	rawPromptInitialWait  = 1 * time.Second
	rawPromptRecoveryWait = 3 * time.Second
	controlCommandTimeout = 10 * time.Second
)
