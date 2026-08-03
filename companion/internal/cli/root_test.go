package cli

import (
	"bytes"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/tomx-sh/esp32-agent/companion/internal/config"
	"github.com/tomx-sh/esp32-agent/companion/internal/hooks"
)

func TestHelpListsMainOperations(t *testing.T) {
	var out bytes.Buffer
	command := New(Options{In: strings.NewReader(""), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs([]string{"--help"})
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"configure", "device", "hooks", "pet", "run", "setup", "status", "sync"} {
		if !strings.Contains(out.String(), name) {
			t.Fatalf("help does not contain %q:\n%s", name, out.String())
		}
	}
}

func TestNoArgumentsStartsInteractiveGuide(t *testing.T) {
	var out bytes.Buffer
	command := New(Options{In: strings.NewReader("9\n"), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs(nil)
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "ESP32 Agent Companion") || !strings.Contains(out.String(), "Set up companion") || !strings.Contains(out.String(), "Configure settings") || !strings.Contains(out.String(), "Sync pet with Codex Desktop") {
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
	codexHome := t.TempDir()
	t.Setenv("CODEX_HOME", codexHome)
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
	hooksPath := filepath.Join(codexHome, "hooks.json")
	if _, err := os.Stat(hooksPath); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("configure unexpectedly created hooks file: %v", err)
	}
	for _, expected := range []string{
		"Configuration saved to " + configPath,
		"Codex hooks were not changed",
		"Hooks file: " + hooksPath,
		"esp32-agent hooks install",
	} {
		if !strings.Contains(out.String(), expected) {
			t.Fatalf("configure output does not contain %q:\n%s", expected, out.String())
		}
	}
}

func TestSetupConfiguresTestsAndInstallsHooks(t *testing.T) {
	server := newDeviceServer(t)
	defer server.Close()
	codexHome := t.TempDir()
	t.Setenv("CODEX_HOME", codexHome)
	configPath := filepath.Join(t.TempDir(), "config.json")
	input := strings.NewReader(server.URL + "\n30s\ncodex\nn\ny\n")
	var out bytes.Buffer
	command := New(Options{
		In:             input,
		Out:            &out,
		ErrOut:         &out,
		Version:        "test",
		ExecutablePath: "/usr/local/bin/esp32-agent",
	})
	command.SetArgs([]string{"--config", configPath, "setup"})
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}

	hooksPath := filepath.Join(codexHome, "hooks.json")
	installed, err := hooks.IsInstalled(hooksPath)
	if err != nil {
		t.Fatal(err)
	}
	if !installed {
		t.Fatal("setup did not install the managed hooks")
	}
	for _, expected := range []string{
		"Testing device connection",
		"Device connected",
		"Setup complete",
		"Hooks file:    " + hooksPath,
		"added commands for 10 Codex lifecycle events",
		"open Settings > Hooks in Codex Desktop",
	} {
		if !strings.Contains(out.String(), expected) {
			t.Fatalf("setup output does not contain %q:\n%s", expected, out.String())
		}
	}
}

func TestStatusShowsHooksFileAndInstallationState(t *testing.T) {
	server := newDeviceServer(t)
	defer server.Close()
	codexHome := t.TempDir()
	t.Setenv("CODEX_HOME", codexHome)
	configPath := filepath.Join(t.TempDir(), "config.json")
	cfg := config.Default()
	cfg.DeviceURL = server.URL
	if err := config.Save(configPath, cfg); err != nil {
		t.Fatal(err)
	}
	hooksPath := filepath.Join(codexHome, "hooks.json")
	if err := hooks.Install(hooksPath, "/usr/local/bin/esp32-agent"); err != nil {
		t.Fatal(err)
	}

	var out bytes.Buffer
	command := New(Options{In: strings.NewReader(""), Out: &out, ErrOut: &out, Version: "test"})
	command.SetArgs([]string{"--config", configPath, "status"})
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "Hooks file:    "+hooksPath) || !strings.Contains(out.String(), "Hooks:         installed (10 lifecycle commands)") {
		t.Fatalf("status does not show installed hooks:\n%s", out.String())
	}
}

func TestHooksInstallExplainsChanges(t *testing.T) {
	hooksPath := filepath.Join(t.TempDir(), "hooks.json")
	var out bytes.Buffer
	command := New(Options{
		In:             strings.NewReader(""),
		Out:            &out,
		ErrOut:         &out,
		Version:        "test",
		ExecutablePath: "/usr/local/bin/esp32-agent",
	})
	command.SetArgs([]string{"hooks", "--path", hooksPath, "install"})
	if err := command.Execute(); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "Added ESP32 Agent commands for 10 Codex lifecycle events to "+hooksPath) ||
		!strings.Contains(out.String(), "Events: SessionStart, UserPromptSubmit, PermissionRequest, PostToolUse, PreCompact, PostCompact, SubagentStart, SubagentStop, Stop, SessionEnd.") {
		t.Fatalf("hooks install does not explain its changes:\n%s", out.String())
	}
}

func TestDeviceMessageSetAndClear(t *testing.T) {
	type message struct {
		Message string `json:"message"`
		Muted   bool   `json:"muted"`
	}
	received := make(chan message, 2)
	server := httptest.NewServer(http.HandlerFunc(func(response http.ResponseWriter, request *http.Request) {
		if request.URL.Path != "/codex/message" {
			http.NotFound(response, request)
			return
		}
		var payload message
		if err := json.NewDecoder(request.Body).Decode(&payload); err != nil {
			t.Errorf("decode message: %v", err)
			response.WriteHeader(http.StatusBadRequest)
			return
		}
		received <- payload
		response.WriteHeader(http.StatusOK)
	}))
	defer server.Close()

	configPath := filepath.Join(t.TempDir(), "config.json")
	cfg := config.Default()
	cfg.DeviceURL = server.URL
	if err := config.Save(configPath, cfg); err != nil {
		t.Fatal(err)
	}

	run := func(args ...string) {
		t.Helper()
		command := New(Options{In: strings.NewReader(""), Out: &bytes.Buffer{}, ErrOut: &bytes.Buffer{}, Version: "test"})
		command.SetArgs(append([]string{"--config", configPath, "device", "message"}, args...))
		if err := command.Execute(); err != nil {
			t.Fatal(err)
		}
	}
	run("set", "Thinking...", "still", "--muted")
	run("clear")

	if got := <-received; got.Message != "Thinking... still" || !got.Muted {
		t.Fatalf("unexpected set payload: %#v", got)
	}
	if got := <-received; got.Message != "" || got.Muted {
		t.Fatalf("unexpected clear payload: %#v", got)
	}
}

func newDeviceServer(t *testing.T) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(response http.ResponseWriter, request *http.Request) {
		response.Header().Set("Content-Type", "application/json")
		var value any
		switch request.URL.Path {
		case "/codex/usage":
			value = map[string]any{"remainingPercent": 57, "resetAt": 0, "resetCredits": 2}
		case "/codex/context":
			value = map[string]any{"remainingPercent": 81}
		case "/codex/message":
			value = map[string]any{"message": "Ready", "muted": false}
		default:
			http.NotFound(response, request)
			return
		}
		if err := json.NewEncoder(response).Encode(value); err != nil {
			t.Errorf("encode device response: %v", err)
		}
	}))
}
