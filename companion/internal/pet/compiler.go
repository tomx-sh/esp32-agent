package pet

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"image"
	"image/color"
	"image/gif"
	_ "image/png"
	"time"

	"github.com/soniakeys/quant/median"
	_ "golang.org/x/image/webp"
)

const (
	frameWidth  = 192
	frameHeight = 208
	columns     = 8
)

type Animation struct {
	Name          string
	GIF           []byte
	CycleDuration time.Duration
}

type Pack struct {
	PetID       string
	DisplayName string
	Version     int
	Hash        string
	Animations  []Animation
}

type Metadata struct {
	PetID       string
	DisplayName string
	Version     int
	Hash        string
}

type animationSpec struct {
	name   string
	row    int
	delays []int
}

var animationSpecs = []animationSpec{
	{name: "idle", row: 0, delays: []int{168, 66, 66, 84, 84, 192}},
	{name: "running-right", row: 1, delays: []int{12, 12, 12, 12, 12, 12, 12, 22}},
	{name: "running-left", row: 2, delays: []int{12, 12, 12, 12, 12, 12, 12, 22}},
	{name: "waving", row: 3, delays: []int{14, 14, 14, 28}},
	{name: "jumping", row: 4, delays: []int{14, 14, 14, 14, 28}},
	{name: "failed", row: 5, delays: []int{14, 14, 14, 14, 14, 14, 14, 24}},
	{name: "waiting", row: 6, delays: []int{15, 15, 15, 15, 15, 26}},
	{name: "running", row: 7, delays: []int{12, 12, 12, 12, 12, 22}},
	{name: "review", row: 8, delays: []int{15, 15, 15, 15, 15, 28}},
}

func Compile(source Source) (Pack, error) {
	atlas, format, err := image.Decode(bytes.NewReader(source.Bytes))
	if err != nil {
		return Pack{}, fmt.Errorf("decode %s pet spritesheet: %w", source.DisplayName, err)
	}
	version, err := spriteVersion(format, atlas.Bounds())
	if err != nil {
		return Pack{}, err
	}
	digest := sha256.Sum256(source.Bytes)
	pack := Pack{
		PetID:       source.ID,
		DisplayName: source.DisplayName,
		Version:     version,
		Hash:        hex.EncodeToString(digest[:]),
		Animations:  make([]Animation, 0, len(animationSpecs)),
	}
	for _, spec := range animationSpecs {
		encoded, err := encodeAnimation(atlas, spec)
		if err != nil {
			return Pack{}, fmt.Errorf("encode %s animation: %w", spec.name, err)
		}
		cycleDuration := time.Duration(0)
		for _, delay := range spec.delays {
			cycleDuration += time.Duration(delay) * 10 * time.Millisecond
		}
		pack.Animations = append(pack.Animations, Animation{Name: spec.name, GIF: encoded, CycleDuration: cycleDuration})
	}
	return pack, nil
}

func BurstDuration(name string) time.Duration {
	if name == "idle" {
		return 0
	}
	for _, spec := range animationSpecs {
		if spec.name != name {
			continue
		}
		cycle := time.Duration(0)
		for _, delay := range spec.delays {
			cycle += time.Duration(delay) * 10 * time.Millisecond
		}
		return cycle * 3
	}
	return 0
}

func Inspect(source Source) (Metadata, error) {
	config, format, err := image.DecodeConfig(bytes.NewReader(source.Bytes))
	if err != nil {
		return Metadata{}, fmt.Errorf("decode %s pet spritesheet metadata: %w", source.DisplayName, err)
	}
	version, err := spriteVersion(format, image.Rect(0, 0, config.Width, config.Height))
	if err != nil {
		return Metadata{}, err
	}
	digest := sha256.Sum256(source.Bytes)
	return Metadata{
		PetID:       source.ID,
		DisplayName: source.DisplayName,
		Version:     version,
		Hash:        hex.EncodeToString(digest[:]),
	}, nil
}

func spriteVersion(format string, bounds image.Rectangle) (int, error) {
	switch {
	case bounds.Dx() == frameWidth*columns && bounds.Dy() == frameHeight*9:
		return 1, nil
	case bounds.Dx() == frameWidth*columns && bounds.Dy() == frameHeight*11:
		return 2, nil
	default:
		return 0, fmt.Errorf("unsupported %s spritesheet dimensions %dx%d; expected 1536x1872 (v1) or 1536x2288 (v2)", format, bounds.Dx(), bounds.Dy())
	}
}

func encodeAnimation(atlas image.Image, spec animationSpec) ([]byte, error) {
	frames := make([]image.Image, 0, len(spec.delays))
	for column := range spec.delays {
		origin := atlas.Bounds().Min.Add(image.Pt(column*frameWidth, spec.row*frameHeight))
		frames = append(frames, crop(atlas, image.Rectangle{Min: origin, Max: origin.Add(image.Pt(frameWidth, frameHeight))}))
	}
	palette := animationPalette(frames)
	output := &gif.GIF{
		Image:     make([]*image.Paletted, 0, len(frames)),
		Delay:     append([]int(nil), spec.delays...),
		Disposal:  make([]byte, 0, len(frames)),
		LoopCount: 0,
		Config:    image.Config{ColorModel: palette, Width: frameWidth, Height: frameHeight},
	}
	for _, frame := range frames {
		output.Image = append(output.Image, paletted(frame, palette))
		output.Disposal = append(output.Disposal, gif.DisposalBackground)
	}
	var buffer bytes.Buffer
	if err := gif.EncodeAll(&buffer, output); err != nil {
		return nil, err
	}
	return buffer.Bytes(), nil
}

func crop(source image.Image, bounds image.Rectangle) image.Image {
	result := image.NewNRGBA(image.Rect(0, 0, bounds.Dx(), bounds.Dy()))
	for y := 0; y < bounds.Dy(); y++ {
		for x := 0; x < bounds.Dx(); x++ {
			result.Set(x, y, source.At(bounds.Min.X+x, bounds.Min.Y+y))
		}
	}
	return result
}

func animationPalette(frames []image.Image) color.Palette {
	opaque := make([]color.Color, 0, len(frames)*frameWidth*frameHeight/2)
	for _, frame := range frames {
		bounds := frame.Bounds()
		for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
			for x := bounds.Min.X; x < bounds.Max.X; x++ {
				value := color.NRGBAModel.Convert(frame.At(x, y)).(color.NRGBA)
				if value.A >= 128 {
					value.A = 255
					opaque = append(opaque, value)
				}
			}
		}
	}
	result := color.Palette{color.NRGBA{A: 0}}
	if len(opaque) == 0 {
		return result
	}
	samples := image.NewNRGBA(image.Rect(0, 0, len(opaque), 1))
	for index, value := range opaque {
		samples.Set(index, 0, value)
	}
	quantized := median.Quantizer(255).Palette(samples).ColorPalette()
	return append(result, quantized...)
}

func paletted(source image.Image, palette color.Palette) *image.Paletted {
	bounds := source.Bounds()
	result := image.NewPaletted(image.Rect(0, 0, bounds.Dx(), bounds.Dy()), palette)
	for y := 0; y < bounds.Dy(); y++ {
		for x := 0; x < bounds.Dx(); x++ {
			value := color.NRGBAModel.Convert(source.At(bounds.Min.X+x, bounds.Min.Y+y)).(color.NRGBA)
			if value.A < 128 {
				result.SetColorIndex(x, y, 0)
				continue
			}
			value.A = 255
			result.SetColorIndex(x, y, uint8(palette.Index(value)))
		}
	}
	return result
}

func DeviceSpriteName(hash, state string) string {
	shortHash := hash
	if len(shortHash) > 12 {
		shortHash = shortHash[:12]
	}
	return "pet-" + shortHash + "-" + state
}
