package pet

import (
	"bytes"
	"image"
	"image/color"
	"image/gif"
	"image/png"
	"testing"
	"time"
)

func TestCompileV1ProducesTimedGIFs(t *testing.T) {
	source := syntheticSource(t, 9)
	pack, err := Compile(source)
	if err != nil {
		t.Fatal(err)
	}
	if pack.Version != 1 || len(pack.Animations) != 9 || len(pack.Hash) != 64 {
		t.Fatalf("unexpected pack: version=%d animations=%d hash=%q", pack.Version, len(pack.Animations), pack.Hash)
	}
	waiting := pack.Animations[6]
	decoded, err := gif.DecodeAll(bytes.NewReader(waiting.GIF))
	if err != nil {
		t.Fatal(err)
	}
	if len(decoded.Image) != 6 || decoded.Config.Width != frameWidth || decoded.Config.Height != frameHeight {
		t.Fatalf("unexpected waiting GIF dimensions or frames: %#v", decoded.Config)
	}
	wantDelays := []int{15, 15, 15, 15, 15, 26}
	for index := range wantDelays {
		if decoded.Delay[index] != wantDelays[index] {
			t.Fatalf("delay %d = %d, want %d", index, decoded.Delay[index], wantDelays[index])
		}
	}
	if waiting.CycleDuration != 1010*time.Millisecond {
		t.Fatalf("waiting cycle duration = %d", waiting.CycleDuration)
	}
	if BurstDuration("waiting") != 3030*time.Millisecond {
		t.Fatalf("waiting burst duration = %s", BurstDuration("waiting"))
	}
}

func TestInspectSupportsV2(t *testing.T) {
	metadata, err := Inspect(syntheticSource(t, 11))
	if err != nil {
		t.Fatal(err)
	}
	if metadata.Version != 2 {
		t.Fatalf("version = %d, want 2", metadata.Version)
	}
}

func syntheticSource(t *testing.T, rows int) Source {
	t.Helper()
	atlas := image.NewNRGBA(image.Rect(0, 0, frameWidth*columns, frameHeight*rows))
	for _, spec := range animationSpecs {
		for column := range spec.delays {
			atlas.SetNRGBA(column*frameWidth, spec.row*frameHeight, color.NRGBA{
				R: uint8(20 + spec.row*20), G: uint8(20 + column*20), B: 180, A: 255,
			})
		}
	}
	var encoded bytes.Buffer
	if err := png.Encode(&encoded, atlas); err != nil {
		t.Fatal(err)
	}
	return Source{ID: "test", DisplayName: "Test", Bytes: encoded.Bytes()}
}
