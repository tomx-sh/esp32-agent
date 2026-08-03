package hooks

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"sync"
	"time"

	"github.com/tomx-sh/esp32-agent/companion/internal/codex"
	"github.com/tomx-sh/esp32-agent/companion/internal/device"
	"github.com/tomx-sh/esp32-agent/companion/internal/state"
)

type Event struct {
	Name           string
	SessionID      string
	TranscriptPath string
}

type Action struct {
	Pet     string
	TTL     time.Duration
	Message string
	Muted   bool
}

type Processor struct {
	Device         *device.Client
	StatePath      string
	ContextEnabled bool
}

func DecodeEvent(reader io.Reader) (Event, error) {
	var raw map[string]json.RawMessage
	if err := json.NewDecoder(reader).Decode(&raw); err != nil {
		return Event{}, fmt.Errorf("decode hook event: %w", err)
	}
	value := func(keys ...string) string {
		for _, key := range keys {
			if data, ok := raw[key]; ok {
				var text string
				if json.Unmarshal(data, &text) == nil {
					return text
				}
			}
		}
		return ""
	}
	event := Event{
		Name:           value("hook_event_name", "hookEventName"),
		SessionID:      value("session_id", "sessionId"),
		TranscriptPath: value("transcript_path", "transcriptPath"),
	}
	if event.Name == "" {
		return Event{}, fmt.Errorf("hook event is missing hook_event_name")
	}
	return event, nil
}

func ActionFor(eventName string) (Action, bool) {
	switch eventName {
	case "SessionStart":
		return Action{Pet: "codex-waving", TTL: 4 * time.Second, Message: "Codex is ready", Muted: true}, true
	case "UserPromptSubmit":
		return Action{Pet: "codex-thinking", Message: "Codex is working", Muted: false}, true
	case "Stop":
		return Action{Pet: "codex-idle", Message: "Ready", Muted: true}, true
	case "SessionEnd":
		return Action{Pet: "codex-idle", Message: "Session ended", Muted: true}, true
	default:
		return Action{}, false
	}
}

func (p Processor) Process(ctx context.Context, event Event) error {
	var messages []string
	if event.TranscriptPath != "" {
		if err := state.Save(p.StatePath, state.State{
			ActiveTranscript: event.TranscriptPath,
			SessionID:        event.SessionID,
		}); err != nil {
			messages = append(messages, err.Error())
		}
	}
	action, ok := ActionFor(event.Name)
	if !ok {
		return nil
	}

	var operations []func() error
	operations = append(operations,
		func() error { return p.Device.SetPet(ctx, action.Pet, action.TTL) },
		func() error { return p.Device.SetMessage(ctx, action.Message, action.Muted) },
	)
	if p.ContextEnabled && event.TranscriptPath != "" {
		if usage, err := codex.ReadContext(event.TranscriptPath); err == nil {
			operations = append(operations, func() error {
				return p.Device.SetContext(ctx, usage.RemainingPercent)
			})
		}
	}

	var wg sync.WaitGroup
	errorsByOperation := make(chan error, len(operations))
	for _, operation := range operations {
		operation := operation
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := operation(); err != nil {
				errorsByOperation <- err
			}
		}()
	}
	wg.Wait()
	close(errorsByOperation)
	for err := range errorsByOperation {
		messages = append(messages, err.Error())
	}
	if len(messages) > 0 {
		return fmt.Errorf("process hook: %s", strings.Join(messages, "; "))
	}
	return nil
}
