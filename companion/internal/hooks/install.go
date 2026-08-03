package hooks

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const managedStatusPrefix = "ESP32 Agent:"

var installedEvents = []string{"SessionStart", "UserPromptSubmit", "Stop", "SessionEnd"}

func ManagedEvents() []string {
	return append([]string(nil), installedEvents...)
}

func DefaultConfigPath() (string, error) {
	if codexHome := os.Getenv("CODEX_HOME"); codexHome != "" {
		return filepath.Join(codexHome, "hooks.json"), nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("find home directory: %w", err)
	}
	return filepath.Join(home, ".codex", "hooks.json"), nil
}

func Install(path, executable string) error {
	root, err := readHookFile(path)
	if err != nil {
		return err
	}
	hookMap := ensureHookMap(root)
	removeManaged(hookMap)
	command := shellQuote(executable) + " hook"
	for _, event := range installedEvents {
		group := map[string]any{
			"hooks": []any{map[string]any{
				"type":          "command",
				"command":       command,
				"timeout":       5,
				"statusMessage": managedStatusPrefix + " syncing " + event,
			}},
		}
		existing, _ := hookMap[event].([]any)
		hookMap[event] = append(existing, group)
	}
	return writeHookFile(path, root)
}

func Uninstall(path string) error {
	root, err := readHookFile(path)
	if err != nil {
		return err
	}
	hookMap, ok := root["hooks"].(map[string]any)
	if !ok {
		return nil
	}
	removeManaged(hookMap)
	return writeHookFile(path, root)
}

func IsInstalled(path string) (bool, error) {
	root, err := readHookFile(path)
	if err != nil {
		return false, err
	}
	hookMap, ok := root["hooks"].(map[string]any)
	if !ok {
		return false, nil
	}
	for _, event := range installedEvents {
		groups, ok := hookMap[event].([]any)
		if !ok || !hasManagedHandler(groups) {
			return false, nil
		}
	}
	return true, nil
}

func hasManagedHandler(groups []any) bool {
	for _, rawGroup := range groups {
		group, ok := rawGroup.(map[string]any)
		if !ok {
			continue
		}
		handlers, ok := group["hooks"].([]any)
		if !ok {
			continue
		}
		for _, rawHandler := range handlers {
			handler, ok := rawHandler.(map[string]any)
			if !ok {
				continue
			}
			status, _ := handler["statusMessage"].(string)
			if strings.HasPrefix(status, managedStatusPrefix) {
				return true
			}
		}
	}
	return false
}

func readHookFile(path string) (map[string]any, error) {
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return map[string]any{}, nil
	}
	if err != nil {
		return nil, fmt.Errorf("read Codex hooks: %w", err)
	}
	var root map[string]any
	if err := json.Unmarshal(data, &root); err != nil {
		return nil, fmt.Errorf("parse Codex hooks: %w", err)
	}
	return root, nil
}

func ensureHookMap(root map[string]any) map[string]any {
	if hooks, ok := root["hooks"].(map[string]any); ok {
		return hooks
	}
	hooks := map[string]any{}
	root["hooks"] = hooks
	return hooks
}

func removeManaged(hookMap map[string]any) {
	for event, rawGroups := range hookMap {
		groups, ok := rawGroups.([]any)
		if !ok {
			continue
		}
		keptGroups := make([]any, 0, len(groups))
		for _, rawGroup := range groups {
			group, ok := rawGroup.(map[string]any)
			if !ok {
				keptGroups = append(keptGroups, rawGroup)
				continue
			}
			rawHandlers, ok := group["hooks"].([]any)
			if !ok {
				keptGroups = append(keptGroups, rawGroup)
				continue
			}
			keptHandlers := make([]any, 0, len(rawHandlers))
			for _, rawHandler := range rawHandlers {
				handler, ok := rawHandler.(map[string]any)
				if !ok {
					keptHandlers = append(keptHandlers, rawHandler)
					continue
				}
				status, _ := handler["statusMessage"].(string)
				if !strings.HasPrefix(status, managedStatusPrefix) {
					keptHandlers = append(keptHandlers, rawHandler)
				}
			}
			if len(keptHandlers) > 0 {
				group["hooks"] = keptHandlers
				keptGroups = append(keptGroups, group)
			}
		}
		if len(keptGroups) == 0 {
			delete(hookMap, event)
		} else {
			hookMap[event] = keptGroups
		}
	}
}

func writeHookFile(path string, root map[string]any) error {
	data, err := json.MarshalIndent(root, "", "  ")
	if err != nil {
		return fmt.Errorf("encode Codex hooks: %w", err)
	}
	data = append(data, '\n')
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return fmt.Errorf("create Codex config directory: %w", err)
	}
	tmp, err := os.CreateTemp(dir, ".hooks-*.tmp")
	if err != nil {
		return fmt.Errorf("create temporary hooks file: %w", err)
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if err := tmp.Chmod(0o600); err != nil {
		tmp.Close()
		return fmt.Errorf("set hooks permissions: %w", err)
	}
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return fmt.Errorf("write hooks: %w", err)
	}
	if err := tmp.Close(); err != nil {
		return fmt.Errorf("close hooks: %w", err)
	}
	if err := os.Rename(tmpPath, path); err != nil {
		return fmt.Errorf("replace hooks: %w", err)
	}
	return nil
}

func shellQuote(value string) string {
	return "'" + strings.ReplaceAll(value, "'", "'\"'\"'") + "'"
}
