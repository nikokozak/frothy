package project

import "path/filepath"

// StarterSourceFiles returns the source files scaffolded by `froth new`
// for the requested board target.
func StarterSourceFiles(board string) map[string]string {
	switch board {
	case "esp32-devkit-v1":
		return workshopStarterSourceFiles()
	default:
		return defaultStarterSourceFiles()
	}
}

func defaultStarterSourceFiles() map[string]string {
	return map[string]string{
		filepath.Join("src", "main.froth"): `note = nil

boot {
  set note = "Hello from Frothy!"
}
`,
	}
}

func workshopStarterSourceFiles() map[string]string {
	return map[string]string{
		filepath.Join("src", "main.froth"): `\ #use "./workshop/lesson.froth"
\ #use "./workshop/game.froth"

to boot [
  lesson.ready:;
  game.reset:
]
`,
		filepath.Join("src", "workshop", "lesson.froth"): `\ #allow-toplevel
status is "booting"
sensor.pin is A0
blink.count is 3
blink.wait is 75
anim.count is 3
anim.wait is 40

to lesson.ready [
  led.off:
  set status to "Workshop starter ready"
]

to lesson.blink [ led.blink: blink.count, blink.wait ]
to lesson.sample [ adc.percent: sensor.pin ]
to lesson.animate with step [ animate: anim.count, anim.wait, step ]
`,
		filepath.Join("src", "workshop", "game.froth"): `\ #allow-toplevel
player is cells(2)
score is 0

to game.reset [
  set player[0] to 0
  set player[1] to 0
  set score to 0
]

to game.step with dx, dy [
  set player[0] to player[0] + dx
  set player[1] to player[1] + dy
]

to game.capture [
  set score to lesson.sample:
]
`,
	}
}
