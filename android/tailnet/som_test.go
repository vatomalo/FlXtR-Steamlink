package flxtrnet

import (
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func somRequest(client *Client, method, path, body, peer string) *httptest.ResponseRecorder {
	request := httptest.NewRequest(method, "http://100.70.1.2:43927/"+strings.Repeat("a", 32)+"/som/"+path, strings.NewReader(body))
	request.RemoteAddr = peer + ":1234"
	response := httptest.NewRecorder()
	client.romHTTP(response, request)
	return response
}
func TestSOMBridgeIsPeerRestrictedAndAllowlisted(t *testing.T) {
	dir := t.TempDir()
	client := &Client{}
	client.rom.port = 1234
	client.som = somBridge{dir: dir, token: strings.Repeat("a", 32), peer: "100.69.7.81"}
	if response := somRequest(client, "POST", "action", "step_right", "100.69.7.82"); response.Code != 403 {
		t.Fatal("unexpected peer accepted", response.Code)
	}
	if response := somRequest(client, "POST", "action", "rm -rf /", "100.69.7.81"); response.Code != 400 {
		t.Fatal("unsafe action accepted", response.Code)
	}
	if response := somRequest(client, "POST", "action", "step_right", "100.69.7.81"); response.Code != 200 {
		t.Fatal("action rejected", response.Code, response.Body.String())
	}
	raw, err := os.ReadFile(filepath.Join(dir, "flxtr-som-ai-action"))
	if err != nil || string(raw) != "step_right\n" {
		t.Fatal("action not delivered", err, string(raw))
	}
}
func TestSOMBridgeRequiresMarkerAndExactWRAM(t *testing.T) {
	dir := t.TempDir()
	client := &Client{}
	client.rom.port = 1234
	client.som = somBridge{dir: dir, token: strings.Repeat("a", 32), peer: "100.69.7.81"}
	if response := somRequest(client, "GET", "wram", "", "100.69.7.81"); response.Code != 409 {
		t.Fatal("missing marker accepted", response.Code)
	}
	os.WriteFile(filepath.Join(dir, "flxtr-som-ai-enabled"), []byte("enabled\n"), 0600)
	os.WriteFile(filepath.Join(dir, "flxtr-som-wram.bin"), make([]byte, somWRAMBytes), 0600)
	if response := somRequest(client, "GET", "wram", "", "100.69.7.81"); response.Code != 200 || response.Body.Len() != somWRAMBytes {
		t.Fatal("WRAM unavailable", response.Code, response.Body.Len())
	}
	if response := somRequest(client, "DELETE", "enabled", "", "100.69.7.81"); response.Code != 200 {
		t.Fatal("marker clear failed")
	}
	if _, err := os.Stat(filepath.Join(dir, "flxtr-som-ai-enabled")); !os.IsNotExist(err) {
		t.Fatal("marker remained")
	}
}
