package codex

import "testing"

func TestParseLimitsConvertsUsedToRemaining(t *testing.T) {
	data := []byte(`{
      "rateLimits": {
        "planType": "plus",
        "primary": {"usedPercent": 43, "windowDurationMins": 10080, "resetsAt": 1786345996}
      },
      "rateLimitResetCredits": {"availableCount": 2, "credits": null}
    }`)
	limits, err := parseLimits(data)
	if err != nil {
		t.Fatal(err)
	}
	if limits.RemainingPercent != 57 || limits.ResetAt != 1786345996 || limits.ResetCredits != 2 || limits.WindowMinutes != 10080 || limits.PlanType != "plus" {
		t.Fatalf("unexpected limits: %#v", limits)
	}
}

func TestParseLimitsRequiresPrimaryWindow(t *testing.T) {
	if _, err := parseLimits([]byte(`{"rateLimits": {}}`)); err == nil {
		t.Fatal("parseLimits unexpectedly accepted a missing primary window")
	}
}

func TestParseSelectedPet(t *testing.T) {
	selected, err := parseSelectedPet([]byte(`{"config":{"desktop":{"selected-avatar-id":"custom:retrobyte"}}}`))
	if err != nil {
		t.Fatal(err)
	}
	if selected != "custom:retrobyte" {
		t.Fatalf("unexpected selected pet %q", selected)
	}

	selected, err = parseSelectedPet([]byte(`{"config":{"desktop":{}}}`))
	if err != nil {
		t.Fatal(err)
	}
	if selected != "codex" {
		t.Fatalf("expected default codex pet, got %q", selected)
	}
}
