package config

import (
	"path/filepath"
	"testing"
)

func TestSaveAndLoad(t *testing.T) {
	path := filepath.Join(t.TempDir(), "nested", "config.json")
	want := Default()
	want.DeviceURL = "http://192.0.2.10"
	want.PollInterval = "10m"
	if err := Save(path, want); err != nil {
		t.Fatal(err)
	}
	got, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if got != want {
		t.Fatalf("Load() = %#v, want %#v", got, want)
	}
}

func TestValidateRejectsUnsafeValues(t *testing.T) {
	tests := []Config{
		{DeviceURL: "esp32-agent.local", PollInterval: "5m", HTTPTimeout: "3s", CodexPath: "codex"},
		{DeviceURL: "http://device", PollInterval: "2s", HTTPTimeout: "3s", CodexPath: "codex"},
		{DeviceURL: "http://device", PollInterval: "5m", HTTPTimeout: "0s", CodexPath: "codex"},
	}
	for _, cfg := range tests {
		if err := cfg.Validate(); err == nil {
			t.Fatalf("Validate(%#v) unexpectedly succeeded", cfg)
		}
	}
}
