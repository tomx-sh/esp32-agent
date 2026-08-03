package cli

import (
	"bytes"
	"path/filepath"
	"strings"
	"testing"

	"github.com/tomx-sh/esp32-agent/companion/internal/config"
)

func TestHelpListsMainOperations(t *testing.T) {
	var out bytes.Buffer
	command := New(Options{In: strings.NewReader(""), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs([]string{"--help"})
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"configure", "device", "hooks", "run", "status", "sync"} {
		if !strings.Contains(out.String(), name) {
			t.Fatalf("help does not contain %q:\n%s", name, out.String())
		}
	}
}

func TestNoArgumentsStartsInteractiveGuide(t *testing.T) {
	var out bytes.Buffer
	command := New(Options{In: strings.NewReader("7\n"), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs(nil)
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "ESP32 Agent Companion") || !strings.Contains(out.String(), "Install Codex Desktop hooks") {
		t.Fatalf("interactive guide missing expected choices:\n%s", out.String())
	}
}

func TestNoArgumentsWithEmptyInputExits(t *testing.T) {
	var out bytes.Buffer
	command := New(Options{In: strings.NewReader(""), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs(nil)
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
}

func TestConfigureUsesInteractiveForm(t *testing.T) {
	configPath := filepath.Join(t.TempDir(), "config.json")
	input := strings.NewReader("http://device.test\n30s\n/usr/local/bin/codex\nn\n")
	var out bytes.Buffer
	command := New(Options{In: input, Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs([]string{"--config", configPath, "configure"})

	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}

	cfg, err := config.Load(configPath)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.DeviceURL != "http://device.test" || cfg.PollInterval != "30s" || cfg.CodexPath != "/usr/local/bin/codex" || cfg.ContextEnabled {
		t.Fatalf("unexpected configuration: %+v", cfg)
	}
}
