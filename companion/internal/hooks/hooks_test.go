package hooks

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDecodeEventSupportsDocumentedFields(t *testing.T) {
	event, err := DecodeEvent(strings.NewReader(`{"hook_event_name":"PostToolUse","session_id":"session-1","transcript_path":"/tmp/session.jsonl","source":"compact","tool_response":{"exit_code":1}}`))
	if err != nil {
		t.Fatal(err)
	}
	if event.Name != "PostToolUse" || event.SessionID != "session-1" || event.TranscriptPath != "/tmp/session.jsonl" || event.Source != "compact" {
		t.Fatalf("unexpected event: %#v", event)
	}
	if !toolFailed(event.ToolResponse) {
		t.Fatal("documented tool_response was not retained")
	}
}

func TestActionForMapsEveryManagedEvent(t *testing.T) {
	tests := []struct {
		name    string
		event   Event
		pet     string
		message string
		muted   bool
		clear   bool
	}{
		{name: "session start", event: Event{Name: "SessionStart"}, pet: "waving", message: "Codex is ready", muted: true},
		{name: "prompt", event: Event{Name: "UserPromptSubmit"}, pet: "running", message: "Thinking...", muted: true},
		{name: "approval", event: Event{Name: "PermissionRequest"}, pet: "waiting", message: "Approval needed"},
		{name: "tool result", event: Event{Name: "PostToolUse", ToolResponse: json.RawMessage(`{"exit_code":0}`)}, message: "Tool finished", muted: true},
		{name: "tool failure", event: Event{Name: "PostToolUse", ToolResponse: json.RawMessage(`{"exit_code":2}`)}, pet: "failed", message: "Tool failed"},
		{name: "pre compact", event: Event{Name: "PreCompact"}, message: "Compacting context", muted: true},
		{name: "post compact", event: Event{Name: "PostCompact"}, pet: "running", message: "Continuing with compacted context", muted: true},
		{name: "subagent start", event: Event{Name: "SubagentStart"}, pet: "running", message: "Subagent working", muted: true},
		{name: "subagent stop", event: Event{Name: "SubagentStop"}, message: "Subagent finished", muted: true},
		{name: "stop", event: Event{Name: "Stop"}, pet: "review", clear: true},
		{name: "session end", event: Event{Name: "SessionEnd"}, pet: "idle", message: "Session ended", muted: true},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			action, ok := ActionFor(test.event)
			if !ok {
				t.Fatal("event did not produce an action")
			}
			if action.Pet != test.pet || action.Message != test.message || action.Muted != test.muted || action.ClearMessage != test.clear {
				t.Fatalf("unexpected action: %#v", action)
			}
		})
	}
}

func TestActionForIgnoresCompactSessionRestart(t *testing.T) {
	if _, ok := ActionFor(Event{Name: "SessionStart", Source: "compact"}); ok {
		t.Fatal("compact session restart should not overwrite PostCompact state")
	}
}

func TestToolFailedUsesOnlyStructuredTopLevelSignals(t *testing.T) {
	tests := []struct {
		response string
		failed   bool
	}{
		{response: `{"isError":true}`, failed: true},
		{response: `{"success":false}`, failed: true},
		{response: `{"exitCode":1}`, failed: true},
		{response: `{"exit_code":0}`},
		{response: `{"output":"error: this is only text"}`},
		{response: `"command failed"`},
	}
	for _, test := range tests {
		if got := toolFailed(json.RawMessage(test.response)); got != test.failed {
			t.Fatalf("toolFailed(%s) = %t, want %t", test.response, got, test.failed)
		}
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
	installed, err := IsInstalled(path)
	if err != nil {
		t.Fatal(err)
	}
	if !installed {
		t.Fatal("managed hooks were not detected after installation")
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
	installed, err = IsInstalled(path)
	if err != nil {
		t.Fatal(err)
	}
	if installed {
		t.Fatal("managed hooks were still detected after uninstall")
	}
	after := string(mustRead(t, path))
	if strings.Contains(after, managedStatusPrefix) || !strings.Contains(after, "other-hook") {
		t.Fatalf("unexpected hooks after uninstall: %s", after)
	}
}

func TestIsInstalledReturnsFalseForMissingFile(t *testing.T) {
	installed, err := IsInstalled(filepath.Join(t.TempDir(), "hooks.json"))
	if err != nil {
		t.Fatal(err)
	}
	if installed {
		t.Fatal("missing hooks file reported as installed")
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
