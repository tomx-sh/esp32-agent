package pet

import (
	"os"
	"path/filepath"
	"testing"
)

func TestResolveCustomPet(t *testing.T) {
	home := t.TempDir()
	t.Setenv("CODEX_HOME", home)
	petDir := filepath.Join(home, "pets", "pixel")
	if err := os.MkdirAll(petDir, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(petDir, "pet.json"), []byte(`{"displayName":"Pixel","spritesheetPath":"pet.png","spriteVersionNumber":2}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(petDir, "pet.png"), []byte("image"), 0o600); err != nil {
		t.Fatal(err)
	}
	source, err := Resolve("custom:pixel")
	if err != nil {
		t.Fatal(err)
	}
	if source.ID != "custom:pixel" || source.DisplayName != "Pixel" || string(source.Bytes) != "image" {
		t.Fatalf("unexpected source: %#v", source)
	}
}

func TestResolveCustomPetRejectsEscapingSpritesheet(t *testing.T) {
	home := t.TempDir()
	t.Setenv("CODEX_HOME", home)
	petDir := filepath.Join(home, "pets", "pixel")
	if err := os.MkdirAll(petDir, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(petDir, "pet.json"), []byte(`{"spritesheetPath":"../../secret.webp"}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := Resolve("custom:pixel"); err == nil {
		t.Fatal("escaping spritesheet path was accepted")
	}
}
