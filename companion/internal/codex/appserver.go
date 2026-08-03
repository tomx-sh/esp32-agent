package codex

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"os/exec"
	"strings"
)

type Limits struct {
	RemainingPercent int
	ResetAt          int64
	ResetCredits     int
	WindowMinutes    int
	PlanType         string
}

type rpcResponse struct {
	ID     int             `json:"id"`
	Result json.RawMessage `json:"result"`
	Error  *struct {
		Code    int    `json:"code"`
		Message string `json:"message"`
	} `json:"error"`
}

type rateLimitsResult struct {
	RateLimits   rateLimitBucket `json:"rateLimits"`
	ResetCredits *struct {
		AvailableCount int `json:"availableCount"`
	} `json:"rateLimitResetCredits"`
}

type rateLimitBucket struct {
	Primary  *rateLimitWindow `json:"primary"`
	PlanType string           `json:"planType"`
}

type rateLimitWindow struct {
	UsedPercent        float64 `json:"usedPercent"`
	WindowDurationMins int     `json:"windowDurationMins"`
	ResetsAt           int64   `json:"resetsAt"`
}

func FetchLimits(ctx context.Context, executable string) (Limits, error) {
	response, err := callAppServer(ctx, executable, "account/rateLimits/read", map[string]any{})
	if err != nil {
		return Limits{}, err
	}
	return parseLimits(response)
}

func SelectedPet(ctx context.Context, executable string) (string, error) {
	response, err := callAppServer(ctx, executable, "config/read", map[string]any{
		"includeLayers": false,
	})
	if err != nil {
		return "", err
	}
	return parseSelectedPet(response)
}

func parseSelectedPet(response []byte) (string, error) {
	var result struct {
		Config struct {
			Desktop map[string]json.RawMessage `json:"desktop"`
		} `json:"config"`
	}
	if err := json.Unmarshal(response, &result); err != nil {
		return "", fmt.Errorf("parse Codex configuration: %w", err)
	}
	selected := "codex"
	if raw, ok := result.Config.Desktop["selected-avatar-id"]; ok {
		var value string
		if json.Unmarshal(raw, &value) == nil && strings.TrimSpace(value) != "" {
			selected = strings.TrimSpace(value)
		}
	}
	return selected, nil
}

func callAppServer(ctx context.Context, executable, method string, params any) (json.RawMessage, error) {
	cmd := exec.CommandContext(ctx, executable, "app-server")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("open app-server stdin: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("open app-server stdout: %w", err)
	}
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start %s app-server: %w", executable, err)
	}
	defer func() {
		_ = stdin.Close()
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		_ = cmd.Wait()
	}()

	encoder := json.NewEncoder(stdin)
	scanner := bufio.NewScanner(stdout)
	scanner.Buffer(make([]byte, 64*1024), 2*1024*1024)

	if err := encoder.Encode(map[string]any{
		"id":     1,
		"method": "initialize",
		"params": map[string]any{"clientInfo": map[string]string{
			"name":    "esp32_agent_companion",
			"title":   "ESP32 Agent Companion",
			"version": "1",
		}},
	}); err != nil {
		return nil, fmt.Errorf("initialize app-server: %w", err)
	}
	if _, err := readResponse(scanner, 1); err != nil {
		return nil, withStderr(err, stderr.String())
	}
	if err := encoder.Encode(map[string]any{"method": "initialized", "params": map[string]any{}}); err != nil {
		return nil, fmt.Errorf("acknowledge app-server initialization: %w", err)
	}
	if err := encoder.Encode(map[string]any{"id": 2, "method": method, "params": params}); err != nil {
		return nil, fmt.Errorf("request Codex %s: %w", method, err)
	}
	response, err := readResponse(scanner, 2)
	if err != nil {
		return nil, withStderr(err, stderr.String())
	}
	return response.Result, nil
}

func readResponse(scanner *bufio.Scanner, id int) (rpcResponse, error) {
	for scanner.Scan() {
		var response rpcResponse
		if err := json.Unmarshal(scanner.Bytes(), &response); err != nil {
			continue
		}
		if response.ID != id {
			continue
		}
		if response.Error != nil {
			return rpcResponse{}, fmt.Errorf("app-server error %d: %s", response.Error.Code, response.Error.Message)
		}
		return response, nil
	}
	if err := scanner.Err(); err != nil {
		return rpcResponse{}, fmt.Errorf("read app-server response: %w", err)
	}
	return rpcResponse{}, io.ErrUnexpectedEOF
}

func parseLimits(data []byte) (Limits, error) {
	var result rateLimitsResult
	if err := json.Unmarshal(data, &result); err != nil {
		return Limits{}, fmt.Errorf("parse Codex rate limits: %w", err)
	}
	if result.RateLimits.Primary == nil {
		return Limits{}, fmt.Errorf("Codex did not return a primary rate-limit window")
	}
	window := result.RateLimits.Primary
	remaining := int(math.Round(100 - window.UsedPercent))
	if remaining < 0 {
		remaining = 0
	}
	if remaining > 100 {
		remaining = 100
	}
	credits := 0
	if result.ResetCredits != nil {
		credits = result.ResetCredits.AvailableCount
	}
	if credits < 0 {
		credits = 0
	}
	if credits > 999 {
		credits = 999
	}
	return Limits{
		RemainingPercent: remaining,
		ResetAt:          window.ResetsAt,
		ResetCredits:     credits,
		WindowMinutes:    window.WindowDurationMins,
		PlanType:         result.RateLimits.PlanType,
	}, nil
}

func withStderr(err error, stderr string) error {
	stderr = strings.TrimSpace(stderr)
	if stderr == "" {
		return err
	}
	return fmt.Errorf("%w: %s", err, stderr)
}
