package cli

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/spf13/cobra"
	"github.com/tomx-sh/esp32-agent/companion/internal/codex"
	"github.com/tomx-sh/esp32-agent/companion/internal/config"
	"github.com/tomx-sh/esp32-agent/companion/internal/device"
	"github.com/tomx-sh/esp32-agent/companion/internal/hooks"
	"github.com/tomx-sh/esp32-agent/companion/internal/state"
)

type Options struct {
	In      io.Reader
	Out     io.Writer
	ErrOut  io.Writer
	Version string
}

type application struct {
	in         io.Reader
	out        io.Writer
	errOut     io.Writer
	version    string
	configPath string
	statePath  string
	verbose    bool
}

func New(options Options) *cobra.Command {
	configPath, _ := config.DefaultPath()
	statePath, _ := state.DefaultPath()
	app := &application{
		in:         options.In,
		out:        options.Out,
		errOut:     options.ErrOut,
		version:    options.Version,
		configPath: configPath,
		statePath:  statePath,
	}
	root := &cobra.Command{
		Use:           "esp32-agent",
		Short:         "Connect Codex Desktop to the ESP32 Agent device",
		Long:          "ESP32 Agent Companion syncs Codex quota, context usage, lifecycle state, and generic messages to the device.",
		Version:       options.Version,
		SilenceErrors: true,
		SilenceUsage:  true,
		RunE: func(cmd *cobra.Command, _ []string) error {
			return app.interactive(cmd.Context())
		},
	}
	root.SetIn(options.In)
	root.SetOut(options.Out)
	root.SetErr(options.ErrOut)
	root.PersistentFlags().StringVar(&app.configPath, "config", configPath, "configuration file path")
	root.PersistentFlags().BoolVarP(&app.verbose, "verbose", "v", false, "show diagnostic details")
	root.AddCommand(
		app.configureCommand(),
		app.syncCommand(),
		app.runCommand(),
		app.statusCommand(),
		app.hookCommand(),
		app.hooksCommand(),
		app.deviceCommand(),
	)
	return root
}

func (a *application) configureCommand() *cobra.Command {
	var deviceURL, pollInterval, codexPath, contextMode string
	cmd := &cobra.Command{
		Use:   "configure",
		Short: "Configure the device and Codex connection",
		RunE: func(cmd *cobra.Command, _ []string) error {
			cfg, err := config.Load(a.configPath)
			if err != nil {
				return err
			}
			changed := cmd.Flags().Changed("device-url") || cmd.Flags().Changed("poll-interval") || cmd.Flags().Changed("codex") || cmd.Flags().Changed("context")
			if !changed {
				return a.promptConfiguration(cmd.Context(), cfg)
			}
			if cmd.Flags().Changed("device-url") {
				cfg.DeviceURL = strings.TrimSpace(deviceURL)
			}
			if cmd.Flags().Changed("poll-interval") {
				cfg.PollInterval = strings.TrimSpace(pollInterval)
			}
			if cmd.Flags().Changed("codex") {
				cfg.CodexPath = strings.TrimSpace(codexPath)
			}
			if cmd.Flags().Changed("context") {
				switch strings.ToLower(contextMode) {
				case "on", "true", "yes":
					cfg.ContextEnabled = true
				case "off", "false", "no":
					cfg.ContextEnabled = false
				default:
					return fmt.Errorf("context must be on or off")
				}
			}
			if err := config.Save(a.configPath, cfg); err != nil {
				return err
			}
			fmt.Fprintf(a.out, "Configuration saved to %s\n", a.configPath)
			return nil
		},
	}
	cmd.Flags().StringVar(&deviceURL, "device-url", "", "device base URL")
	cmd.Flags().StringVar(&pollInterval, "poll-interval", "", "quota polling interval, for example 5m")
	cmd.Flags().StringVar(&codexPath, "codex", "", "Codex executable path")
	cmd.Flags().StringVar(&contextMode, "context", "", "experimental context reader: on or off")
	return cmd
}

func (a *application) syncCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "sync",
		Short: "Sync Codex quota and active context now",
		RunE: func(cmd *cobra.Command, _ []string) error {
			cfg, client, err := a.loadClient()
			if err != nil {
				return err
			}
			return a.syncAll(cmd.Context(), cfg, client)
		},
	}
}

func (a *application) runCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "run",
		Short: "Run the foreground Codex-to-device bridge",
		RunE: func(cmd *cobra.Command, _ []string) error {
			return a.runBridge(cmd.Context())
		},
	}
}

func (a *application) statusCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "status",
		Short: "Show configuration and live device state",
		RunE: func(cmd *cobra.Command, _ []string) error {
			return a.showStatus(cmd.Context())
		},
	}
}

func (a *application) hookCommand() *cobra.Command {
	return &cobra.Command{
		Use:   "hook",
		Short: "Process one Codex lifecycle hook from stdin",
		Args:  cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			event, err := hooks.DecodeEvent(cmd.InOrStdin())
			if err != nil {
				return err
			}
			cfg, client, err := a.loadClient()
			if err != nil {
				return err
			}
			processor := hooks.Processor{Device: client, StatePath: a.statePath, ContextEnabled: cfg.ContextEnabled}
			if err := processor.Process(cmd.Context(), event); err != nil {
				if a.verbose {
					a.logError(err)
				}
			}
			return nil
		},
	}
}

func (a *application) hooksCommand() *cobra.Command {
	var hooksPath string
	cmd := &cobra.Command{Use: "hooks", Short: "Install or remove global Codex Desktop hooks"}
	defaultPath, _ := hooks.DefaultConfigPath()
	cmd.PersistentFlags().StringVar(&hooksPath, "path", defaultPath, "Codex hooks.json path")
	cmd.AddCommand(&cobra.Command{
		Use:   "install",
		Short: "Install lifecycle hooks for every Codex project",
		RunE: func(_ *cobra.Command, _ []string) error {
			if err := a.installHooks(hooksPath); err != nil {
				return err
			}
			fmt.Fprintf(a.out, "Codex hooks installed in %s\nReview and trust them from /hooks in Codex.\n", hooksPath)
			return nil
		},
	})
	cmd.AddCommand(&cobra.Command{
		Use:   "uninstall",
		Short: "Remove only ESP32 Agent hook handlers",
		RunE: func(_ *cobra.Command, _ []string) error {
			if err := hooks.Uninstall(hooksPath); err != nil {
				return err
			}
			fmt.Fprintf(a.out, "ESP32 Agent hooks removed from %s\n", hooksPath)
			return nil
		},
	})
	cmd.AddCommand(&cobra.Command{
		Use:   "path",
		Short: "Print the Codex hooks file path",
		Run: func(_ *cobra.Command, _ []string) {
			fmt.Fprintln(a.out, hooksPath)
		},
	})
	return cmd
}

func (a *application) deviceCommand() *cobra.Command {
	cmd := &cobra.Command{Use: "device", Short: "Inspect or control the ESP32 device directly"}
	cmd.AddCommand(&cobra.Command{
		Use:   "test",
		Short: "Test connectivity and display current values",
		RunE: func(ctxCmd *cobra.Command, _ []string) error {
			return a.showDevice(ctxCmd.Context())
		},
	})
	var ttl time.Duration
	pet := &cobra.Command{
		Use:   "pet NAME",
		Short: "Show a bundled pet sprite",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			_, client, err := a.loadClient()
			if err != nil {
				return err
			}
			return client.SetPet(cmd.Context(), args[0], ttl)
		},
	}
	pet.Flags().DurationVar(&ttl, "ttl", 0, "restore the previous sprite after this duration")
	cmd.AddCommand(pet)
	var muted bool
	message := &cobra.Command{
		Use:   "message TEXT",
		Short: "Set the generic Codex message",
		Args:  cobra.MinimumNArgs(1),
		RunE: func(ctxCmd *cobra.Command, args []string) error {
			_, client, err := a.loadClient()
			if err != nil {
				return err
			}
			return client.SetMessage(ctxCmd.Context(), strings.Join(args, " "), muted)
		},
	}
	message.Flags().BoolVar(&muted, "muted", false, "render the message in the muted style")
	cmd.AddCommand(message)
	return cmd
}

func (a *application) loadClient() (config.Config, *device.Client, error) {
	cfg, err := config.Load(a.configPath)
	if err != nil {
		return config.Config{}, nil, err
	}
	timeout, _ := cfg.RequestTimeout()
	client, err := device.New(cfg.DeviceURL, timeout)
	if err != nil {
		return config.Config{}, nil, err
	}
	return cfg, client, nil
}

func (a *application) syncQuota(ctx context.Context, cfg config.Config, client *device.Client) error {
	rpcCtx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	limits, err := codex.FetchLimits(rpcCtx, cfg.CodexPath)
	if err != nil {
		return err
	}
	if err := client.SetQuota(ctx, device.Quota{
		RemainingPercent: limits.RemainingPercent, ResetAt: limits.ResetAt, ResetCredits: limits.ResetCredits,
	}); err != nil {
		return err
	}
	fmt.Fprintf(a.out, "Quota synced: %d%% left", limits.RemainingPercent)
	if limits.ResetAt > 0 {
		fmt.Fprintf(a.out, ", resets %s", time.Unix(limits.ResetAt, 0).Local().Format(time.RFC822))
	}
	fmt.Fprintf(a.out, ", %d reset credits\n", limits.ResetCredits)
	return nil
}

func (a *application) runQuotaUpdate(ctx context.Context, cfg config.Config, client *device.Client) {
	if err := a.syncQuota(ctx, cfg, client); err != nil {
		a.logError(err)
	}
}

func (a *application) syncAll(ctx context.Context, cfg config.Config, client *device.Client) error {
	if err := a.syncQuota(ctx, cfg, client); err != nil {
		return err
	}
	if !cfg.ContextEnabled {
		return nil
	}
	usage, available, err := a.currentContext()
	if err != nil {
		return err
	}
	if !available {
		fmt.Fprintln(a.out, "Context: unavailable until a Codex hook supplies an active transcript")
		return nil
	}
	if err := client.SetContext(ctx, usage.RemainingPercent); err != nil {
		return err
	}
	fmt.Fprintf(a.out, "Context: %d%% remaining\n", usage.RemainingPercent)
	return nil
}

func (a *application) runBridge(ctx context.Context) error {
	cfg, client, err := a.loadClient()
	if err != nil {
		return err
	}
	interval, _ := cfg.PollDuration()
	fmt.Fprintf(a.out, "Bridge running for %s (quota every %s). Press Ctrl-C to stop.\n", cfg.DeviceURL, interval)
	a.runQuotaUpdate(ctx, cfg, client)
	quotaTicker := time.NewTicker(interval)
	defer quotaTicker.Stop()
	contextTicker := time.NewTicker(2 * time.Second)
	defer contextTicker.Stop()
	lastContext := -1
	for {
		select {
		case <-ctx.Done():
			fmt.Fprintln(a.out, "Bridge stopped")
			return nil
		case <-quotaTicker.C:
			a.runQuotaUpdate(ctx, cfg, client)
		case <-contextTicker.C:
			if !cfg.ContextEnabled {
				continue
			}
			usage, available, err := a.currentContext()
			if err != nil {
				a.logError(err)
				continue
			}
			if available && usage.RemainingPercent != lastContext {
				if err := client.SetContext(ctx, usage.RemainingPercent); err != nil {
					a.logError(err)
					continue
				}
				lastContext = usage.RemainingPercent
				fmt.Fprintf(a.out, "Context synced: %d%% remaining\n", lastContext)
			}
		}
	}
}

func (a *application) installHooks(path string) error {
	executable, err := os.Executable()
	if err != nil {
		return fmt.Errorf("find companion executable: %w", err)
	}
	executable, _ = filepath.Abs(executable)
	if strings.Contains(executable, string(filepath.Separator)+"go-build"+string(filepath.Separator)) {
		return fmt.Errorf("hooks cannot use a temporary go run binary; build or install esp32-agent first")
	}
	return hooks.Install(path, executable)
}

func (a *application) currentContext() (codex.ContextUsage, bool, error) {
	value, err := state.Load(a.statePath)
	if err != nil {
		return codex.ContextUsage{}, false, err
	}
	if value.ActiveTranscript == "" {
		return codex.ContextUsage{}, false, nil
	}
	usage, err := codex.ReadContext(value.ActiveTranscript)
	if errors.Is(err, codex.ErrContextUnavailable) {
		return codex.ContextUsage{}, false, nil
	}
	if err != nil {
		return codex.ContextUsage{}, false, err
	}
	return usage, true, nil
}

func (a *application) showDevice(ctx context.Context) error {
	_, client, err := a.loadClient()
	if err != nil {
		return err
	}
	snapshot, err := client.Snapshot(ctx)
	if err != nil {
		return err
	}
	fmt.Fprintln(a.out, "Device connected")
	fmt.Fprintf(a.out, "  Quota:   %d%% left\n", snapshot.Quota.RemainingPercent)
	fmt.Fprintf(a.out, "  Context: %d%% remaining\n", snapshot.Context.RemainingPercent)
	fmt.Fprintf(a.out, "  Credits: %d\n", snapshot.Quota.ResetCredits)
	fmt.Fprintf(a.out, "  Message: %s\n", strconv.Quote(snapshot.Message.Message))
	return nil
}

func (a *application) showStatus(ctx context.Context) error {
	cfg, _, err := a.loadClient()
	if err != nil {
		return err
	}
	fmt.Fprintln(a.out, "Companion")
	fmt.Fprintf(a.out, "  Version:       %s\n", a.version)
	fmt.Fprintf(a.out, "  Config:        %s\n", a.configPath)
	fmt.Fprintf(a.out, "  Device URL:    %s\n", cfg.DeviceURL)
	fmt.Fprintf(a.out, "  Poll interval: %s\n", cfg.PollInterval)
	fmt.Fprintf(a.out, "  Context:       %t (experimental transcript reader)\n", cfg.ContextEnabled)
	value, stateErr := state.Load(a.statePath)
	if stateErr == nil && value.ActiveTranscript != "" {
		fmt.Fprintf(a.out, "  Transcript:    %s\n", value.ActiveTranscript)
	}
	fmt.Fprintln(a.out)
	return a.showDevice(ctx)
}

func (a *application) logError(err error) {
	if err == nil {
		return
	}
	fmt.Fprintln(a.errOut, "Warning:", err)
}
