package cli

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"charm.land/huh/v2"
	"github.com/charmbracelet/x/term"
	"github.com/tomx-sh/esp32-agent/companion/internal/config"
	"github.com/tomx-sh/esp32-agent/companion/internal/hooks"
)

type interactiveAction string

const (
	actionSetup     interactiveAction = "setup"
	actionConfigure interactiveAction = "configure"
	actionTest      interactiveAction = "test"
	actionSync      interactiveAction = "sync"
	actionRun       interactiveAction = "run"
	actionHooks     interactiveAction = "hooks"
	actionStatus    interactiveAction = "status"
	actionExit      interactiveAction = "exit"
)

func (a *application) interactive(ctx context.Context) error {
	for {
		action, err := a.promptAction(ctx)
		if promptCanceled(err) {
			return nil
		}
		if err != nil {
			return err
		}

		switch action {
		case actionSetup:
			path, err := hooks.DefaultConfigPath()
			if err == nil {
				err = a.runSetup(ctx, path)
			}
			if err != nil && !promptCanceled(err) {
				a.logError(err)
			}
		case actionConfigure:
			cfg, err := config.Load(a.configPath)
			if err == nil {
				err = a.promptConfiguration(ctx, cfg)
			}
			if err != nil && !promptCanceled(err) {
				a.logError(err)
			}
		case actionTest:
			if err := a.showDevice(ctx); err != nil {
				a.logError(err)
			}
		case actionSync:
			cfg, client, err := a.loadClient()
			if err == nil {
				err = a.syncAll(ctx, cfg, client)
			}
			if err != nil {
				a.logError(err)
			}
		case actionRun:
			return a.runBridge(ctx)
		case actionHooks:
			path, err := hooks.DefaultConfigPath()
			if err == nil {
				err = a.installHooks(path)
			}
			if err != nil {
				a.logError(err)
			} else {
				a.reportHooksInstalled(path)
			}
		case actionStatus:
			if err := a.showStatus(ctx); err != nil {
				a.logError(err)
			}
		case actionExit:
			return nil
		}
	}
}

func (a *application) promptAction(ctx context.Context) (interactiveAction, error) {
	var action interactiveAction
	err := a.runPromptForm(ctx,
		huh.NewGroup(
			huh.NewSelect[interactiveAction]().
				Title("ESP32 Agent Companion").
				Description("Connect Codex Desktop activity and usage to your device.").
				Options(
					huh.NewOption("Set up companion", actionSetup),
					huh.NewOption("Configure settings", actionConfigure),
					huh.NewOption("Test device connection", actionTest),
					huh.NewOption("Sync Codex data now", actionSync),
					huh.NewOption("Run foreground bridge", actionRun),
					huh.NewOption("Install Codex Desktop hooks", actionHooks),
					huh.NewOption("Show status", actionStatus),
					huh.NewOption("Exit", actionExit),
				).
				Value(&action),
		),
	)
	if err != nil {
		return "", err
	}
	return action, nil
}

func (a *application) promptConfiguration(ctx context.Context, cfg config.Config) error {
	cfg, err := a.promptConfigurationValues(ctx, cfg)
	if err != nil {
		return err
	}
	if err := config.Save(a.configPath, cfg); err != nil {
		return err
	}
	a.reportConfigurationSaved()
	return nil
}

func (a *application) promptConfigurationValues(ctx context.Context, cfg config.Config) (config.Config, error) {
	err := a.runPromptForm(ctx,
		huh.NewGroup(
			huh.NewInput().
				Title("Device URL").
				Description("Base URL advertised by the ESP32 Agent.").
				Value(&cfg.DeviceURL).
				Validate(func(value string) error {
					candidate := cfg
					candidate.DeviceURL = strings.TrimSpace(value)
					return candidate.Validate()
				}),
			huh.NewInput().
				Title("Quota poll interval").
				Description("Go duration of at least 10s, for example 5m.").
				Value(&cfg.PollInterval).
				Validate(func(value string) error {
					candidate := cfg
					candidate.PollInterval = strings.TrimSpace(value)
					_, err := candidate.PollDuration()
					return err
				}),
			huh.NewInput().
				Title("Codex executable").
				Description("Command or absolute path used to start Codex App Server.").
				Value(&cfg.CodexPath).
				Validate(func(value string) error {
					if strings.TrimSpace(value) == "" {
						return errors.New("Codex executable cannot be empty")
					}
					return nil
				}),
			huh.NewConfirm().
				Title("Enable the experimental context gauge?").
				Description("Reads the active Codex transcript using an isolated best-effort parser.").
				Affirmative("Yes").
				Negative("No").
				Value(&cfg.ContextEnabled),
		).
			Title("Configure companion").
			Description("Review the current values and save when finished."),
	)
	if err != nil {
		return config.Config{}, err
	}

	cfg.DeviceURL = strings.TrimSpace(cfg.DeviceURL)
	cfg.PollInterval = strings.TrimSpace(cfg.PollInterval)
	cfg.CodexPath = strings.TrimSpace(cfg.CodexPath)
	return cfg, nil
}

func (a *application) runSetup(ctx context.Context, hooksPath string) error {
	cfg, err := config.Load(a.configPath)
	if err != nil {
		return err
	}
	cfg, err = a.promptConfigurationValues(ctx, cfg)
	if err != nil {
		return err
	}
	if err := config.Save(a.configPath, cfg); err != nil {
		return err
	}
	fmt.Fprintf(a.out, "Configuration saved to %s\n\n", a.configPath)
	fmt.Fprintln(a.out, "Testing device connection...")
	if err := a.showDevice(ctx); err != nil {
		return fmt.Errorf("device test failed: %w", err)
	}

	install := true
	err = a.runPromptForm(ctx,
		huh.NewGroup(
			huh.NewConfirm().
				Title("Install Codex Desktop hooks?").
				Description(fmt.Sprintf("Adds commands for %s to %s.", managedEventsSummary(), hooksPath)).
				Affirmative("Install").
				Negative("Skip").
				Value(&install),
		),
	)
	if err != nil {
		return err
	}
	if install {
		if err := a.installHooks(hooksPath); err != nil {
			return err
		}
	}

	fmt.Fprintln(a.out, "\nSetup complete")
	fmt.Fprintf(a.out, "  Configuration: %s\n", a.configPath)
	fmt.Fprintln(a.out, "  Device:        connected")
	fmt.Fprintf(a.out, "  Hooks file:    %s\n", hooksPath)
	if install {
		fmt.Fprintf(a.out, "  Hooks:         added commands for %s\n", managedEventsSummary())
		fmt.Fprintln(a.out, "Next: open /hooks in Codex Desktop and review/trust the new definitions.")
	} else {
		fmt.Fprintln(a.out, "  Hooks:         not installed (skipped)")
		fmt.Fprintln(a.out, "Next: run `esp32-agent hooks install` when you want to enable lifecycle updates.")
	}
	return nil
}

func (a *application) runPromptForm(ctx context.Context, groups ...*huh.Group) error {
	accessible := os.Getenv("ACCESSIBLE") != "" || !isTerminal(a.in) || !isTerminal(a.out)
	input := a.in
	var trackedInput *singleByteReader
	if accessible {
		// Huh's accessible fields create a scanner per prompt. Limiting reads to
		// one byte prevents buffered readers used by tests and pipelines from
		// consuming answers intended for later fields.
		trackedInput = &singleByteReader{reader: input}
		input = trackedInput
	}
	err := huh.NewForm(groups...).
		WithInput(input).
		WithOutput(a.out).
		WithAccessible(accessible).
		RunWithContext(ctx)
	if err == nil && trackedInput != nil && trackedInput.emptyEOF() {
		return io.EOF
	}
	return err
}

func isTerminal(value any) bool {
	file, ok := value.(*os.File)
	return ok && term.IsTerminal(file.Fd())
}

type singleByteReader struct {
	reader     io.Reader
	bytesRead  int
	reachedEOF bool
}

func (r *singleByteReader) Read(buffer []byte) (int, error) {
	if len(buffer) == 0 {
		return 0, nil
	}
	count, err := r.reader.Read(buffer[:1])
	r.bytesRead += count
	if errors.Is(err, io.EOF) {
		r.reachedEOF = true
	}
	return count, err
}

func (r *singleByteReader) emptyEOF() bool {
	return r.bytesRead == 0 && r.reachedEOF
}

func promptCanceled(err error) bool {
	return errors.Is(err, huh.ErrUserAborted) || errors.Is(err, context.Canceled) || errors.Is(err, io.EOF)
}
