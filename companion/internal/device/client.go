package device

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

type Client struct {
	baseURL string
	http    *http.Client
}

type Usage struct {
	Percent      int   `json:"percent"`
	ResetAt      int64 `json:"resetAt"`
	ResetCredits int   `json:"resetCredits"`
}

type Context struct {
	RemainingPercent int `json:"remainingPercent"`
}

type Message struct {
	Message string `json:"message"`
	Muted   bool   `json:"muted"`
}

type Snapshot struct {
	Usage   Usage
	Context Context
	Message Message
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

func (c *Client) SetUsage(ctx context.Context, usage Usage) error {
	return c.post(ctx, "/codex/usage", usage)
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

func (c *Client) Snapshot(ctx context.Context) (Snapshot, error) {
	var result Snapshot
	if err := c.get(ctx, "/codex/usage", &result.Usage); err != nil {
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
