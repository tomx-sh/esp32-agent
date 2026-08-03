package hooks

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDecodeEventSupportsDocumentedFields(t *testing.T) {
	event, err := DecodeEvent(strings.NewReader(`{"hook_event_name":"Stop","session_id":"session-1","transcript_path":"/tmp/session.jsonl"}`))
	if err != nil {
		t.Fatal(err)
	}
	if event.Name != "Stop" || event.SessionID != "session-1" || event.TranscriptPath != "/tmp/session.jsonl" {
		t.Fatalf("unexpected event: %#v", event)
	}
}

func TestInstallAndUninstallPreserveOtherHooks(t *testing.T) {
	path := filepath.Join(t.TempDir(), "hooks.json")
	original := `{"custom":"keep","hooks":{"Stop":[{"hooks":[{"type":"command","command":"other-hook","statusMessage":"Other"}]}]}}`
	if err := os.WriteFile(path, []byte(original), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := Install(path, "/opt/esp32 agent/bin/esp32-agent"); err != nil {
		t.Fatal(err)
	}
	root := readJSON(t, path)
	if root["custom"] != "keep" {
		t.Fatalf("unknown top-level field was not preserved: %#v", root)
	}
	data, _ := os.ReadFile(path)
	if !strings.Contains(string(data), `'/opt/esp32 agent/bin/esp32-agent' hook`) {
		t.Fatalf("installed command is not safely quoted: %s", data)
	}
	if err := Install(path, "/opt/esp32 agent/bin/esp32-agent"); err != nil {
		t.Fatal(err)
	}
	if count := strings.Count(string(mustRead(t, path)), managedStatusPrefix); count != len(installedEvents) {
		t.Fatalf("reinstall produced %d managed handlers, want %d", count, len(installedEvents))
	}
	if err := Uninstall(path); err != nil {
		t.Fatal(err)
	}
	after := string(mustRead(t, path))
	if strings.Contains(after, managedStatusPrefix) || !strings.Contains(after, "other-hook") {
		t.Fatalf("unexpected hooks after uninstall: %s", after)
	}
}

func readJSON(t *testing.T, path string) map[string]any {
	t.Helper()
	var value map[string]any
	if err := json.Unmarshal(mustRead(t, path), &value); err != nil {
		t.Fatal(err)
	}
	return value
}

func mustRead(t *testing.T, path string) []byte {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return data
}
