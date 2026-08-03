package cli

import (
	"bytes"
	"strings"
	"testing"
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
