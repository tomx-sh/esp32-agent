package device

import (
	"context"
	"encoding/json"
	"io"
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

func TestPetPackUploadAndActivation(t *testing.T) {
	var uploadedName string
	var uploadedBody []byte
	var activated PetPack
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch {
		case r.URL.Path == "/sprites/upload":
			uploadedName = r.URL.Query().Get("name")
			if err := r.ParseMultipartForm(1024); err != nil {
				t.Errorf("parse upload: %v", err)
				return
			}
			file, _, err := r.FormFile("file")
			if err != nil {
				t.Errorf("read upload: %v", err)
				return
			}
			defer file.Close()
			uploadedBody, _ = io.ReadAll(file)
		case r.URL.Path == "/pet-pack" && r.Method == http.MethodPost:
			if err := json.NewDecoder(r.Body).Decode(&activated); err != nil {
				t.Errorf("decode pet pack: %v", err)
			}
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	client, _ := New(server.URL, time.Second)
	if err := client.UploadSprite(context.Background(), "pet-abc-idle", []byte("GIF89a")); err != nil {
		t.Fatal(err)
	}
	pack := PetPack{PetID: "codex", DisplayName: "Codex", SourceHash: "abc", SpriteVersion: 2, Sprites: map[string]string{"idle": "pet-abc-idle"}}
	if err := client.ActivatePetPack(context.Background(), pack); err != nil {
		t.Fatal(err)
	}
	if uploadedName != "pet-abc-idle" || string(uploadedBody) != "GIF89a" {
		t.Fatalf("unexpected upload name=%q body=%q", uploadedName, uploadedBody)
	}
	if activated.PetID != "codex" || activated.Sprites["idle"] != "pet-abc-idle" {
		t.Fatalf("unexpected activated pack: %#v", activated)
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
