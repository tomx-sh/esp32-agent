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
	UsedPercent   int
	ResetAt       int64
	ResetCredits  int
	WindowMinutes int
	PlanType      string
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
	cmd := exec.CommandContext(ctx, executable, "app-server")
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return Limits{}, fmt.Errorf("open app-server stdin: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return Limits{}, fmt.Errorf("open app-server stdout: %w", err)
	}
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Start(); err != nil {
		return Limits{}, fmt.Errorf("start %s app-server: %w", executable, err)
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
		return Limits{}, fmt.Errorf("initialize app-server: %w", err)
	}
	if _, err := readResponse(scanner, 1); err != nil {
		return Limits{}, withStderr(err, stderr.String())
	}
	if err := encoder.Encode(map[string]any{"method": "initialized", "params": map[string]any{}}); err != nil {
		return Limits{}, fmt.Errorf("acknowledge app-server initialization: %w", err)
	}
	if err := encoder.Encode(map[string]any{"id": 2, "method": "account/rateLimits/read", "params": map[string]any{}}); err != nil {
		return Limits{}, fmt.Errorf("request Codex rate limits: %w", err)
	}
	response, err := readResponse(scanner, 2)
	if err != nil {
		return Limits{}, withStderr(err, stderr.String())
	}
	return parseLimits(response.Result)
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
	used := int(math.Round(window.UsedPercent))
	if used < 0 {
		used = 0
	}
	if used > 100 {
		used = 100
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
		UsedPercent:   used,
		ResetAt:       window.ResetsAt,
		ResetCredits:  credits,
		WindowMinutes: window.WindowDurationMins,
		PlanType:      result.RateLimits.PlanType,
	}, nil
}

func withStderr(err error, stderr string) error {
	stderr = strings.TrimSpace(stderr)
	if stderr == "" {
		return err
	}
	return fmt.Errorf("%w: %s", err, stderr)
}
