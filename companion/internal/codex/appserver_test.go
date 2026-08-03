package codex

import "testing"

func TestParseLimits(t *testing.T) {
	data := []byte(`{
      "rateLimits": {
        "planType": "plus",
        "primary": {"usedPercent": 17.4, "windowDurationMins": 10080, "resetsAt": 1786345996}
      },
      "rateLimitResetCredits": {"availableCount": 2, "credits": null}
    }`)
	limits, err := parseLimits(data)
	if err != nil {
		t.Fatal(err)
	}
	if limits.UsedPercent != 17 || limits.ResetAt != 1786345996 || limits.ResetCredits != 2 || limits.WindowMinutes != 10080 || limits.PlanType != "plus" {
		t.Fatalf("unexpected limits: %#v", limits)
	}
}

func TestParseLimitsRequiresPrimaryWindow(t *testing.T) {
	if _, err := parseLimits([]byte(`{"rateLimits": {}}`)); err == nil {
		t.Fatal("parseLimits unexpectedly accepted a missing primary window")
	}
}
