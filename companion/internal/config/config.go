package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	defaultDeviceURL    = "http://esp32-agent.local"
	defaultPollInterval = "5m"
	defaultHTTPTimeout  = "3s"
)

type Config struct {
	DeviceURL      string `json:"deviceUrl"`
	PollInterval   string `json:"pollInterval"`
	HTTPTimeout    string `json:"httpTimeout"`
	CodexPath      string `json:"codexPath"`
	ContextEnabled bool   `json:"contextEnabled"`
}

func Default() Config {
	return Config{
		DeviceURL:      defaultDeviceURL,
		PollInterval:   defaultPollInterval,
		HTTPTimeout:    defaultHTTPTimeout,
		CodexPath:      "codex",
		ContextEnabled: true,
	}
}

func DefaultPath() (string, error) {
	dir, err := os.UserConfigDir()
	if err != nil {
		return "", fmt.Errorf("find user config directory: %w", err)
	}
	return filepath.Join(dir, "esp32-agent", "config.json"), nil
}

func Load(path string) (Config, error) {
	cfg := Default()
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return cfg, nil
	}
	if err != nil {
		return Config{}, fmt.Errorf("read config: %w", err)
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return Config{}, fmt.Errorf("parse config: %w", err)
	}
	if err := cfg.Validate(); err != nil {
		return Config{}, err
	}
	return cfg, nil
}

func Save(path string, cfg Config) error {
	if err := cfg.Validate(); err != nil {
		return err
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return fmt.Errorf("encode config: %w", err)
	}
	data = append(data, '\n')
	return writeAtomic(path, data, 0o600)
}

func (c Config) Validate() error {
	parsed, err := url.Parse(c.DeviceURL)
	if err != nil || parsed.Host == "" || (parsed.Scheme != "http" && parsed.Scheme != "https") {
		return fmt.Errorf("device URL must be an absolute http or https URL")
	}
	if _, err := c.PollDuration(); err != nil {
		return err
	}
	if _, err := c.RequestTimeout(); err != nil {
		return err
	}
	if strings.TrimSpace(c.CodexPath) == "" {
		return fmt.Errorf("Codex executable cannot be empty")
	}
	return nil
}

func (c Config) PollDuration() (time.Duration, error) {
	d, err := time.ParseDuration(c.PollInterval)
	if err != nil || d < 10*time.Second {
		return 0, fmt.Errorf("poll interval must be a duration of at least 10s")
	}
	return d, nil
}

func (c Config) RequestTimeout() (time.Duration, error) {
	d, err := time.ParseDuration(c.HTTPTimeout)
	if err != nil || d <= 0 {
		return 0, fmt.Errorf("HTTP timeout must be a positive duration")
	}
	return d, nil
}

func writeAtomic(path string, data []byte, mode os.FileMode) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return fmt.Errorf("create config directory: %w", err)
	}
	tmp, err := os.CreateTemp(dir, ".config-*.tmp")
	if err != nil {
		return fmt.Errorf("create temporary config: %w", err)
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if err := tmp.Chmod(mode); err != nil {
		tmp.Close()
		return fmt.Errorf("set config permissions: %w", err)
	}
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return fmt.Errorf("write config: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close config: %w", err)
	}
	if err := os.Rename(tmpPath, path); err != nil {
		return fmt.Errorf("replace config: %w", err)
	}
	return nil
}
