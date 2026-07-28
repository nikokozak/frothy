package boardmanifest

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type manifest struct {
	Name        string         `json:"name"`
	Chip        string         `json:"chip"`
	Target      string         `json:"target"`
	Profile     string         `json:"profile"`
	Bootsel     string         `json:"bootsel"`
	Cores       int            `json:"cores"`
	Peripherals []string       `json:"peripherals"`
	Pins        map[string]int `json:"pins"`
}

func loadManifest(t *testing.T, path string) manifest {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	var m manifest
	if err := json.Unmarshal(raw, &m); err != nil {
		t.Fatalf("parse %s: %v", path, err)
	}
	return m
}

func checkRequired(t *testing.T, label string, m manifest) {
	t.Helper()
	if m.Name == "" {
		t.Errorf("%s: name is empty", label)
	}
	if m.Chip == "" {
		t.Errorf("%s: chip is empty", label)
	}
	if m.Target == "" {
		t.Errorf("%s: target is empty", label)
	}
	if m.Profile == "" {
		t.Errorf("%s: profile is empty", label)
	}
	if m.Peripherals == nil {
		t.Errorf("%s: peripherals missing (use [] if none)", label)
	}
	if m.Cores < 1 {
		t.Errorf("%s: cores must be positive", label)
	}
	if m.Pins == nil {
		t.Errorf("%s: pins missing (use {} if none)", label)
	}
}

func repoRoot(t *testing.T) string {
	t.Helper()
	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	return filepath.Join(wd, "..", "..")
}

func TestHostBoardManifest(t *testing.T) {
	m := loadManifest(t, filepath.Join(repoRoot(t), "boards", "host", "board.json"))
	checkRequired(t, "host", m)
	if _, ok := m.Pins["$led_builtin"]; !ok {
		t.Errorf("host: pins missing $led_builtin")
	}
}

func TestEsp32DevkitV1BoardManifest(t *testing.T) {
	m := loadManifest(t, filepath.Join(repoRoot(t), "boards", "esp32_devkit_v1", "board.json"))
	checkRequired(t, "esp32_devkit_v1", m)
	for _, key := range []string{"$led_builtin", "$boot_button", "$a0", "uart_tx", "uart_rx", "uart_baud"} {
		if _, ok := m.Pins[key]; !ok {
			t.Errorf("esp32_devkit_v1: pins missing %s", key)
		}
	}
	want := map[string]bool{"gpio": true, "adc": true, "uart": true}
	got := map[string]bool{}
	for _, p := range m.Peripherals {
		got[p] = true
	}
	for k := range want {
		if !got[k] {
			t.Errorf("esp32_devkit_v1: peripherals missing %q", k)
		}
	}
}

func TestSeeedXiaoEsp32s3BoardManifest(t *testing.T) {
	m := loadManifest(t, filepath.Join(repoRoot(t), "boards", "seeed_xiao_esp32s3", "board.json"))
	checkRequired(t, "seeed_xiao_esp32s3", m)
	if m.Chip != "esp32s3" || m.Target != "esp-idf" || m.Profile != "esp32_plain" {
		t.Errorf("seeed_xiao_esp32s3: got chip=%q target=%q profile=%q", m.Chip, m.Target, m.Profile)
	}
	wantPins := map[string]int{
		"$led_builtin": 21,
		"$boot_button": 0,
		"$a0":          1,
		"$sda":         5,
		"$scl":         6,
		"uart_tx":      43,
		"uart_rx":      44,
		"uart_baud":    115200,
	}
	for key, want := range wantPins {
		if got, ok := m.Pins[key]; !ok || got != want {
			t.Errorf("seeed_xiao_esp32s3: pin %s = %d, want %d", key, got, want)
		}
	}
}

func TestArduinoNanoRp2040ConnectBoardManifest(t *testing.T) {
	boardDir := filepath.Join(repoRoot(t), "boards", "arduino_nano_rp2040_connect")
	m := loadManifest(t, filepath.Join(boardDir, "board.json"))
	checkRequired(t, "arduino_nano_rp2040_connect", m)
	if m.Chip != "rp2040" || m.Target != "arduino-rp2040" || m.Profile != "rp2040_nina" {
		t.Errorf("arduino_nano_rp2040_connect: got chip=%q target=%q profile=%q", m.Chip, m.Target, m.Profile)
	}
	if m.Bootsel == "" {
		t.Error("arduino_nano_rp2040_connect: bootsel instructions missing")
	}
	wantPins := map[string]int{
		"$led_builtin": 6,
		"$a0":          26,
		"$sda":         12,
		"$scl":         13,
	}
	for key, want := range wantPins {
		if got, ok := m.Pins[key]; !ok || got != want {
			t.Errorf("arduino_nano_rp2040_connect: pin %s = %d, want %d", key, got, want)
		}
	}
	peripherals := strings.Join(m.Peripherals, ",")
	if !strings.Contains(peripherals, "net") || !strings.Contains(peripherals, "ble") {
		t.Errorf("arduino_nano_rp2040_connect: radio peripherals missing from %q", peripherals)
	}
	boardMK, err := os.ReadFile(filepath.Join(boardDir, "board.mk"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(boardMK), "FR_PROFILE_TARGET_FLASH_BYTES=16777216u") {
		t.Error("arduino_nano_rp2040_connect: 16 MB profile capacity override missing")
	}
}

func TestSeeedXiaoRp2040BoardManifest(t *testing.T) {
	boardDir := filepath.Join(repoRoot(t), "boards", "seeed_xiao_rp2040")
	m := loadManifest(t, filepath.Join(boardDir, "board.json"))
	checkRequired(t, "seeed_xiao_rp2040", m)
	if m.Chip != "rp2040" || m.Target != "arduino-rp2040" || m.Profile != "rp2040_plain" {
		t.Errorf("seeed_xiao_rp2040: got chip=%q target=%q profile=%q", m.Chip, m.Target, m.Profile)
	}
	if m.Bootsel == "" {
		t.Error("seeed_xiao_rp2040: bootsel instructions missing")
	}
	wantPins := map[string]int{
		"$led_builtin":      17,
		"$led_active_level": 0,
		"$a0":               26,
		"$sda":              6,
		"$scl":              7,
	}
	for key, want := range wantPins {
		if got, ok := m.Pins[key]; !ok || got != want {
			t.Errorf("seeed_xiao_rp2040: pin %s = %d, want %d", key, got, want)
		}
	}
	for _, peripheral := range m.Peripherals {
		if peripheral == "net" || peripheral == "ble" {
			t.Errorf("seeed_xiao_rp2040: unexpected radio peripheral %q", peripheral)
		}
	}
	boardMK, err := os.ReadFile(filepath.Join(boardDir, "board.mk"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(boardMK), "BOARD_FLASH_BYTES := 2097152") {
		t.Error("seeed_xiao_rp2040: flash size missing")
	}
}
