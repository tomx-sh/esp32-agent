package cli

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/spf13/cobra"
	"github.com/tomx-sh/esp32-agent/companion/internal/codex"
	"github.com/tomx-sh/esp32-agent/companion/internal/config"
	"github.com/tomx-sh/esp32-agent/companion/internal/device"
	petassets "github.com/tomx-sh/esp32-agent/companion/internal/pet"
)

func (a *application) petCommand() *cobra.Command {
	command := &cobra.Command{
		Use:   "pet",
		Short: "Synchronize the selected Codex Desktop pet",
	}
	command.AddCommand(&cobra.Command{
		Use:   "sync",
		Short: "Convert and upload the selected Codex Desktop pet",
		Args:  cobra.NoArgs,
		RunE: func(command *cobra.Command, _ []string) error {
			cfg, client, err := a.loadClient()
			if err != nil {
				return err
			}
			return a.syncPet(command.Context(), cfg, client)
		},
	})
	command.AddCommand(&cobra.Command{
		Use:   "status",
		Short: "Compare the selected Desktop pet with the device",
		Args:  cobra.NoArgs,
		RunE: func(command *cobra.Command, _ []string) error {
			cfg, client, err := a.loadClient()
			if err != nil {
				return err
			}
			return a.showPetStatus(command.Context(), cfg, client)
		},
	})
	return command
}

func (a *application) selectedPet(ctx context.Context, cfg config.Config) (petassets.Source, error) {
	rpcCtx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	selectedID, err := codex.SelectedPet(rpcCtx, cfg.CodexPath)
	if err != nil {
		return petassets.Source{}, err
	}
	source, err := petassets.Resolve(selectedID)
	if err != nil {
		return petassets.Source{}, err
	}
	return source, nil
}

func (a *application) syncPet(ctx context.Context, cfg config.Config, client *device.Client) error {
	source, err := a.selectedPet(ctx, cfg)
	if err != nil {
		return err
	}
	metadata, err := petassets.Inspect(source)
	if err != nil {
		return err
	}
	current, err := client.PetPack(ctx)
	if err != nil {
		return fmt.Errorf("check device pet-pack support (flash the current firmware first): %w", err)
	}
	if current.PetID == metadata.PetID && current.SourceHash == metadata.Hash {
		a.cleanupOldPetSprites(ctx, client, current.Sprites)
		fmt.Fprintf(a.out, "%s is already synchronized (sprite-sheet v%d).\n", metadata.DisplayName, metadata.Version)
		return nil
	}

	fmt.Fprintf(a.out, "Converting %s sprite-sheet v%d to GIFs...\n", metadata.DisplayName, metadata.Version)
	pack, err := petassets.Compile(source)
	if err != nil {
		return err
	}
	sprites := make(map[string]string, len(pack.Animations))
	for index, animation := range pack.Animations {
		name := petassets.DeviceSpriteName(pack.Hash, animation.Name)
		fmt.Fprintf(a.out, "Uploading %s (%d/%d)...\n", animation.Name, index+1, len(pack.Animations))
		if err := client.UploadSprite(ctx, name, animation.GIF); err != nil {
			return fmt.Errorf("upload %s animation: %w", animation.Name, err)
		}
		sprites[animation.Name] = name
	}
	devicePack := device.PetPack{
		PetID:         pack.PetID,
		DisplayName:   pack.DisplayName,
		SourceHash:    pack.Hash,
		SpriteVersion: pack.Version,
		Sprites:       sprites,
	}
	if err := client.ActivatePetPack(ctx, devicePack); err != nil {
		return fmt.Errorf("activate synchronized pet: %w", err)
	}
	a.cleanupOldPetSprites(ctx, client, sprites)
	fmt.Fprintf(a.out, "Synchronized %s: %d correctly timed GIF animations are active on the device.\n", pack.DisplayName, len(pack.Animations))
	return nil
}

func (a *application) cleanupOldPetSprites(ctx context.Context, client *device.Client, keep map[string]string) {
	required := make(map[string]bool, len(keep))
	for _, name := range keep {
		required[name] = true
	}
	listing, err := client.Sprites(ctx)
	if err != nil {
		if a.verbose {
			a.logError(fmt.Errorf("list old pet sprites: %w", err))
		}
		return
	}
	for _, sprite := range listing.Items {
		if strings.HasPrefix(sprite.Name, "pet-") && !required[sprite.Name] {
			if err := client.DeleteSprite(ctx, sprite.Name); err != nil && a.verbose {
				a.logError(fmt.Errorf("delete old pet sprite %s: %w", sprite.Name, err))
			}
		}
	}
}

func (a *application) showPetStatus(ctx context.Context, cfg config.Config, client *device.Client) error {
	source, err := a.selectedPet(ctx, cfg)
	if err != nil {
		return err
	}
	metadata, err := petassets.Inspect(source)
	if err != nil {
		return err
	}
	current, err := client.PetPack(ctx)
	if err != nil {
		return fmt.Errorf("read device pet pack (flash the current firmware first): %w", err)
	}
	fmt.Fprintln(a.out, "Codex Desktop pet")
	fmt.Fprintf(a.out, "  Selected:      %s (%s)\n", metadata.DisplayName, metadata.PetID)
	fmt.Fprintf(a.out, "  Sprite sheet:  v%d\n", metadata.Version)
	fmt.Fprintf(a.out, "  Source hash:   %s\n", metadata.Hash[:12])
	if current.PetID == "" {
		fmt.Fprintln(a.out, "  Device:        no synchronized pet")
		fmt.Fprintln(a.out, "  Sync status:   synchronization required")
		return nil
	}
	fmt.Fprintf(a.out, "  Device:        %s (%s, v%d)\n", current.DisplayName, current.PetID, current.SpriteVersion)
	if current.PetID == metadata.PetID && current.SourceHash == metadata.Hash {
		fmt.Fprintln(a.out, "  Sync status:   up to date")
	} else {
		fmt.Fprintln(a.out, "  Sync status:   synchronization required")
	}
	return nil
}
