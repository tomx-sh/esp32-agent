package pet

import (
	"os"
	"path/filepath"
	"testing"
)

func TestInstalledCodexPet(t *testing.T) {
	if os.Getenv("TEST_INSTALLED_CODEX_PET") == "" {
		t.Skip("set TEST_INSTALLED_CODEX_PET=1 to validate the locally installed Codex pet")
	}
	source, err := Resolve("codex")
	if err != nil {
		t.Fatal(err)
	}
	pack, err := Compile(source)
	if err != nil {
		t.Fatal(err)
	}
	if pack.Version != 2 || len(pack.Animations) != 9 {
		t.Fatalf("unexpected installed Codex pet: version=%d animations=%d", pack.Version, len(pack.Animations))
	}
	for _, animation := range pack.Animations {
		if len(animation.GIF) < 6 || string(animation.GIF[:6]) != "GIF89a" {
			t.Fatalf("%s did not compile to GIF89a", animation.Name)
		}
		if len(animation.GIF) > 512*1024 {
			t.Fatalf("%s is too large for the device: %d bytes", animation.Name, len(animation.GIF))
		}
		t.Logf("%s: %d bytes", animation.Name, len(animation.GIF))
		if outputDir := os.Getenv("TEST_PET_OUTPUT_DIR"); outputDir != "" {
			if err := os.MkdirAll(outputDir, 0o700); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(filepath.Join(outputDir, animation.Name+".gif"), animation.GIF, 0o600); err != nil {
				t.Fatal(err)
			}
		}
	}
}
