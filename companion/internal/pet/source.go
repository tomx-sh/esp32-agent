package pet

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
)

type Source struct {
	ID          string
	DisplayName string
	Description string
	Bytes       []byte
}

type customManifest struct {
	ID                  string `json:"id"`
	DisplayName         string `json:"displayName"`
	Description         string `json:"description"`
	SpritesheetPath     string `json:"spritesheetPath"`
	SpriteVersionNumber *int   `json:"spriteVersionNumber"`
}

func Resolve(selectedID string) (Source, error) {
	selectedID = strings.TrimSpace(selectedID)
	if selectedID == "" {
		selectedID = "codex"
	}
	if customID, ok := strings.CutPrefix(selectedID, "custom:"); ok {
		return resolveCustom(customID)
	}
	return resolveBuiltin(selectedID)
}

func resolveCustom(id string) (Source, error) {
	if !validPetID(id) {
		return Source{}, fmt.Errorf("invalid custom Codex pet ID %q", id)
	}
	home, err := codexHome()
	if err != nil {
		return Source{}, err
	}
	petDir := filepath.Join(home, "pets", id)
	manifestPath := filepath.Join(petDir, "pet.json")
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return Source{}, fmt.Errorf("read custom pet manifest %s: %w", manifestPath, err)
	}
	var manifest customManifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		return Source{}, fmt.Errorf("parse custom pet manifest %s: %w", manifestPath, err)
	}
	if manifest.SpriteVersionNumber != nil && *manifest.SpriteVersionNumber != 1 && *manifest.SpriteVersionNumber != 2 {
		return Source{}, fmt.Errorf("custom pet %q has unsupported spriteVersionNumber %d", id, *manifest.SpriteVersionNumber)
	}
	sheetName := strings.TrimSpace(manifest.SpritesheetPath)
	if sheetName == "" {
		sheetName = "spritesheet.webp"
	}
	sheetPath, err := safeChildPath(petDir, sheetName)
	if err != nil {
		return Source{}, fmt.Errorf("custom pet %q spritesheet: %w", id, err)
	}
	sheet, err := os.ReadFile(sheetPath)
	if err != nil {
		return Source{}, fmt.Errorf("read custom pet spritesheet %s: %w", sheetPath, err)
	}
	displayName := strings.TrimSpace(manifest.DisplayName)
	if displayName == "" {
		displayName = strings.TrimSpace(manifest.ID)
	}
	if displayName == "" {
		displayName = id
	}
	return Source{ID: "custom:" + id, DisplayName: displayName, Description: manifest.Description, Bytes: sheet}, nil
}

func resolveBuiltin(id string) (Source, error) {
	if !validPetID(id) {
		return Source{}, fmt.Errorf("invalid built-in Codex pet ID %q", id)
	}
	archive, err := desktopASARPath()
	if err != nil {
		return Source{}, err
	}
	pattern := regexp.MustCompile(`^webview/assets/` + regexp.QuoteMeta(id) + `-spritesheet-v[0-9]+-[^/]+\.webp$`)
	_, data, err := readMatchingASARFile(archive, pattern)
	if err != nil {
		return Source{}, fmt.Errorf("find built-in pet %q in %s: %w", id, archive, err)
	}
	return Source{ID: id, DisplayName: builtinDisplayName(id), Bytes: data}, nil
}

func desktopASARPath() (string, error) {
	if override := strings.TrimSpace(os.Getenv("CODEX_DESKTOP_ASAR")); override != "" {
		if info, err := os.Stat(override); err == nil && !info.IsDir() {
			return override, nil
		}
		return "", fmt.Errorf("CODEX_DESKTOP_ASAR does not point to a file: %s", override)
	}
	if runtime.GOOS != "darwin" {
		return "", fmt.Errorf("built-in Codex pet discovery is currently supported on macOS; custom pets remain supported")
	}
	home, _ := os.UserHomeDir()
	candidates := []string{
		"/Applications/ChatGPT.app/Contents/Resources/app.asar",
		"/Applications/Codex.app/Contents/Resources/app.asar",
		filepath.Join(home, "Applications", "ChatGPT.app", "Contents", "Resources", "app.asar"),
		filepath.Join(home, "Applications", "Codex.app", "Contents", "Resources", "app.asar"),
	}
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, nil
		}
	}
	return "", errors.New("could not find the Codex Desktop app; expected ChatGPT.app or Codex.app in Applications")
}

func codexHome() (string, error) {
	if value := strings.TrimSpace(os.Getenv("CODEX_HOME")); value != "" {
		return value, nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("find home directory: %w", err)
	}
	return filepath.Join(home, ".codex"), nil
}

func safeChildPath(parent, name string) (string, error) {
	if filepath.IsAbs(name) {
		return "", errors.New("path must be relative")
	}
	parentAbs, err := filepath.Abs(parent)
	if err != nil {
		return "", err
	}
	childAbs, err := filepath.Abs(filepath.Join(parent, name))
	if err != nil {
		return "", err
	}
	parentReal, err := filepath.EvalSymlinks(parentAbs)
	if err != nil {
		return "", err
	}
	childReal, err := filepath.EvalSymlinks(childAbs)
	if err != nil {
		return "", err
	}
	rel, err := filepath.Rel(parentReal, childReal)
	if err != nil || rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
		return "", errors.New("path escapes the pet directory")
	}
	return childReal, nil
}

func validPetID(value string) bool {
	if value == "" || len(value) > 64 {
		return false
	}
	for _, r := range value {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '-' || r == '_' {
			continue
		}
		return false
	}
	return true
}

func builtinDisplayName(id string) string {
	switch id {
	case "bsod":
		return "BSOD"
	case "null-signal":
		return "Null Signal"
	default:
		if id == "" {
			return id
		}
		return strings.ToUpper(id[:1]) + id[1:]
	}
}
