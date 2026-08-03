package codex

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
)

const (
	BaselineTokens    = int64(12_000)
	maxTranscriptTail = int64(4 * 1024 * 1024)
)

var ErrContextUnavailable = errors.New("context usage is unavailable")

type ContextUsage struct {
	TotalTokens        int64
	ModelContextWindow int64
	RemainingPercent   int
}

type transcriptEntry struct {
	Type    string          `json:"type"`
	Payload json.RawMessage `json:"payload"`
}

type tokenPayload struct {
	Type string `json:"type"`
	Info *struct {
		LastTokenUsage struct {
			TotalTokens int64 `json:"total_tokens"`
		} `json:"last_token_usage"`
		ModelContextWindow int64 `json:"model_context_window"`
	} `json:"info"`
}

func ReadContext(path string) (ContextUsage, error) {
	if path == "" {
		return ContextUsage{}, ErrContextUnavailable
	}
	file, err := os.Open(path)
	if err != nil {
		return ContextUsage{}, fmt.Errorf("open transcript: %w", err)
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return ContextUsage{}, fmt.Errorf("stat transcript: %w", err)
	}
	start := int64(0)
	if info.Size() > maxTranscriptTail {
		start = info.Size() - maxTranscriptTail
	}
	if _, err := file.Seek(start, 0); err != nil {
		return ContextUsage{}, fmt.Errorf("seek transcript: %w", err)
	}
	data, err := readAtMost(file, maxTranscriptTail)
	if err != nil {
		return ContextUsage{}, fmt.Errorf("read transcript: %w", err)
	}
	if start > 0 {
		if index := bytes.IndexByte(data, '\n'); index >= 0 {
			data = data[index+1:]
		}
	}
	lines := bytes.Split(data, []byte{'\n'})
	for i := len(lines) - 1; i >= 0; i-- {
		usage, ok := parseContextLine(lines[i])
		if ok {
			return usage, nil
		}
	}
	return ContextUsage{}, ErrContextUnavailable
}

func RemainingPercent(totalTokens, modelContextWindow int64) (int, error) {
	effectiveWindow := modelContextWindow - BaselineTokens
	if effectiveWindow <= 0 {
		return 0, fmt.Errorf("invalid model context window %d", modelContextWindow)
	}
	used := totalTokens - BaselineTokens
	if used < 0 {
		used = 0
	}
	remaining := effectiveWindow - used
	if remaining < 0 {
		remaining = 0
	}
	percent := int(math.Round(100 * float64(remaining) / float64(effectiveWindow)))
	if percent < 0 {
		percent = 0
	}
	if percent > 100 {
		percent = 100
	}
	return percent, nil
}

func parseContextLine(line []byte) (ContextUsage, bool) {
	if !bytes.Contains(line, []byte(`"token_count"`)) {
		return ContextUsage{}, false
	}
	var entry transcriptEntry
	if err := json.Unmarshal(line, &entry); err != nil || len(entry.Payload) == 0 {
		return ContextUsage{}, false
	}
	var payload tokenPayload
	if err := json.Unmarshal(entry.Payload, &payload); err != nil || payload.Type != "token_count" || payload.Info == nil {
		return ContextUsage{}, false
	}
	total := payload.Info.LastTokenUsage.TotalTokens
	window := payload.Info.ModelContextWindow
	if total <= 0 || window <= 0 {
		return ContextUsage{}, false
	}
	percent, err := RemainingPercent(total, window)
	if err != nil {
		return ContextUsage{}, false
	}
	return ContextUsage{TotalTokens: total, ModelContextWindow: window, RemainingPercent: percent}, true
}

func readAtMost(file *os.File, limit int64) ([]byte, error) {
	return io.ReadAll(io.LimitReader(file, limit))
}
