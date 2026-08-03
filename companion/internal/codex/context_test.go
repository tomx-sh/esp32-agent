package codex

import (
	"os"
	"path/filepath"
	"testing"
)

func TestReadContextUsesLatestTokenCount(t *testing.T) {
	path := filepath.Join(t.TempDir(), "session.jsonl")
	data := "{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"last_token_usage\":{\"total_tokens\":40000},\"model_context_window\":258400}}}\n" +
		"{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"info\":{\"last_token_usage\":{\"total_tokens\":156064},\"model_context_window\":258400}}}\n"
	if err := os.WriteFile(path, []byte(data), 0o600); err != nil {
		t.Fatal(err)
	}
	usage, err := ReadContext(path)
	if err != nil {
		t.Fatal(err)
	}
	if usage.TotalTokens != 156064 || usage.ModelContextWindow != 258400 || usage.RemainingPercent != 42 {
		t.Fatalf("unexpected context usage: %#v", usage)
	}
}

func TestRemainingPercentBounds(t *testing.T) {
	tests := []struct {
		total, window int64
		want          int
	}{
		{0, 258400, 100},
		{12000, 258400, 100},
		{258400, 258400, 0},
		{400000, 258400, 0},
	}
	for _, test := range tests {
		got, err := RemainingPercent(test.total, test.window)
		if err != nil {
			t.Fatal(err)
		}
		if got != test.want {
			t.Fatalf("RemainingPercent(%d, %d) = %d, want %d", test.total, test.window, got, test.want)
		}
	}
}
