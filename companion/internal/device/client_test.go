package device

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestClientWritesDevicePayloads(t *testing.T) {
	received := map[string]map[string]any{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Errorf("decode %s: %v", r.URL.Path, err)
		}
		received[r.URL.Path] = body
		w.WriteHeader(http.StatusOK)
	}))
	defer server.Close()
	client, err := New(server.URL, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	ctx := context.Background()
	if err := client.SetQuota(ctx, Quota{RemainingPercent: 58, ResetAt: 1234, ResetCredits: 2}); err != nil {
		t.Fatal(err)
	}
	if err := client.SetContext(ctx, 68); err != nil {
		t.Fatal(err)
	}
	if err := client.SetMessage(ctx, "Codex is working", true); err != nil {
		t.Fatal(err)
	}
	if err := client.SetPet(ctx, "codex-thinking", 1500*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	if received["/codex/usage"]["remainingPercent"] != float64(58) || received["/codex/usage"]["resetCredits"] != float64(2) {
		t.Fatalf("unexpected usage payload: %#v", received["/codex/usage"])
	}
	if received["/codex/context"]["remainingPercent"] != float64(68) {
		t.Fatalf("unexpected context payload: %#v", received["/codex/context"])
	}
	if received["/pet"]["ttlMs"] != float64(1500) {
		t.Fatalf("unexpected pet payload: %#v", received["/pet"])
	}
}

func TestSnapshot(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case "/codex/usage":
			_, _ = w.Write([]byte(`{"remainingPercent":83,"resetAt":1234,"resetCredits":3}`))
		case "/codex/context":
			_, _ = w.Write([]byte(`{"remainingPercent":61}`))
		case "/codex/message":
			_, _ = w.Write([]byte(`{"message":"Ready","muted":true}`))
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	client, _ := New(server.URL, time.Second)
	snapshot, err := client.Snapshot(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if snapshot.Quota.RemainingPercent != 83 || snapshot.Context.RemainingPercent != 61 || snapshot.Message.Message != "Ready" {
		t.Fatalf("unexpected snapshot: %#v", snapshot)
	}
}
