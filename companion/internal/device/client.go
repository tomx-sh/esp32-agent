package device

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/url"
	"strings"
	"time"
)

type Client struct {
	baseURL string
	http    *http.Client
}

type Quota struct {
	RemainingPercent int   `json:"remainingPercent"`
	ResetAt          int64 `json:"resetAt"`
	ResetCredits     int   `json:"resetCredits"`
}

type Context struct {
	RemainingPercent int `json:"remainingPercent"`
}

type Message struct {
	Message string `json:"message"`
	Muted   bool   `json:"muted"`
}

type Snapshot struct {
	Quota   Quota
	Context Context
	Message Message
}

type Sprite struct {
	Name      string `json:"name"`
	Size      int    `json:"size"`
	Active    bool   `json:"active"`
	IsDefault bool   `json:"isDefault"`
}

type Sprites struct {
	StorageAvailable bool     `json:"storageAvailable"`
	ActiveSprite     string   `json:"activeSprite"`
	DefaultSprite    string   `json:"defaultSpriteName"`
	Items            []Sprite `json:"sprites"`
}

type PetPack struct {
	PetID         string            `json:"petId"`
	DisplayName   string            `json:"displayName"`
	SourceHash    string            `json:"sourceHash"`
	SpriteVersion int               `json:"spriteVersion"`
	Sprites       map[string]string `json:"sprites"`
}

func New(baseURL string, timeout time.Duration) (*Client, error) {
	parsed, err := url.Parse(baseURL)
	if err != nil || parsed.Host == "" || (parsed.Scheme != "http" && parsed.Scheme != "https") {
		return nil, fmt.Errorf("invalid device URL %q", baseURL)
	}
	return &Client{
		baseURL: strings.TrimRight(baseURL, "/"),
		http:    &http.Client{Timeout: timeout},
	}, nil
}

func (c *Client) SetQuota(ctx context.Context, quota Quota) error {
	return c.post(ctx, "/codex/usage", quota)
}

func (c *Client) SetContext(ctx context.Context, remaining int) error {
	return c.post(ctx, "/codex/context", Context{RemainingPercent: remaining})
}

func (c *Client) SetMessage(ctx context.Context, message string, muted bool) error {
	return c.post(ctx, "/codex/message", Message{Message: message, Muted: muted})
}

func (c *Client) SetPet(ctx context.Context, name string, ttl time.Duration) error {
	payload := struct {
		Name  string `json:"name"`
		TTLMS int64  `json:"ttlMs"`
	}{Name: name, TTLMS: ttl.Milliseconds()}
	return c.post(ctx, "/pet", payload)
}

func (c *Client) UploadSprite(ctx context.Context, name string, data []byte) error {
	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	part, err := writer.CreateFormFile("file", name+".gif")
	if err != nil {
		return fmt.Errorf("create sprite upload: %w", err)
	}
	if _, err := part.Write(data); err != nil {
		return fmt.Errorf("write sprite upload: %w", err)
	}
	if err := writer.Close(); err != nil {
		return fmt.Errorf("finish sprite upload: %w", err)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.baseURL+"/sprites/upload?name="+url.QueryEscape(name), &body)
	if err != nil {
		return fmt.Errorf("create sprite upload request: %w", err)
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	return c.do(req, nil)
}

func (c *Client) Sprites(ctx context.Context) (Sprites, error) {
	var result Sprites
	if err := c.get(ctx, "/sprites", &result); err != nil {
		return Sprites{}, err
	}
	return result, nil
}

func (c *Client) DeleteSprite(ctx context.Context, name string) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodDelete, c.baseURL+"/sprites?name="+url.QueryEscape(name), nil)
	if err != nil {
		return fmt.Errorf("create sprite delete request: %w", err)
	}
	return c.do(req, nil)
}

func (c *Client) ActivatePetPack(ctx context.Context, pack PetPack) error {
	return c.post(ctx, "/pet-pack", pack)
}

func (c *Client) PetPack(ctx context.Context) (PetPack, error) {
	var result PetPack
	if err := c.get(ctx, "/pet-pack", &result); err != nil {
		return PetPack{}, err
	}
	return result, nil
}

func (c *Client) Snapshot(ctx context.Context) (Snapshot, error) {
	var result Snapshot
	if err := c.get(ctx, "/codex/usage", &result.Quota); err != nil {
		return Snapshot{}, err
	}
	if err := c.get(ctx, "/codex/context", &result.Context); err != nil {
		return Snapshot{}, err
	}
	if err := c.get(ctx, "/codex/message", &result.Message); err != nil {
		return Snapshot{}, err
	}
	return result, nil
}

func (c *Client) post(ctx context.Context, path string, value any) error {
	body, err := json.Marshal(value)
	if err != nil {
		return fmt.Errorf("encode device request: %w", err)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.baseURL+path, bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create device request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")
	return c.do(req, nil)
}

func (c *Client) get(ctx context.Context, path string, target any) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, c.baseURL+path, nil)
	if err != nil {
		return fmt.Errorf("create device request: %w", err)
	}
	return c.do(req, target)
}

func (c *Client) do(req *http.Request, target any) error {
	resp, err := c.http.Do(req)
	if err != nil {
		return fmt.Errorf("device request %s: %w", req.URL.Path, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		message, _ := io.ReadAll(io.LimitReader(resp.Body, 1024))
		return fmt.Errorf("device request %s returned %s: %s", req.URL.Path, resp.Status, strings.TrimSpace(string(message)))
	}
	if target == nil {
		return nil
	}
	if err := json.NewDecoder(resp.Body).Decode(target); err != nil {
		return fmt.Errorf("decode device response %s: %w", req.URL.Path, err)
	}
	return nil
}
