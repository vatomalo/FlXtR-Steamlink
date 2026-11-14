package flxtrnet

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
)

const somWRAMBytes = 0x20000

var somActions = map[string]bool{
	"right": true, "left": true, "up": true, "down": true, "attack": true, "open_ring": true,
	"confirm": true, "cancel": true, "next_ring": true, "prev_ring": true, "step_right": true,
	"step_left": true, "step_up": true, "step_down": true, "wait": true, "snapshot": true,
	"menu_x": true, "charge": true, "dash_right": true, "dash_left": true, "dash_up": true,
	"dash_down": true, "block": true, "clone_enemy": true, "undo_spawn": true, "ai_on": true, "ai_off": true,
}
var somMessage = regexp.MustCompile(`^[A-Za-z0-9 .,!?:/%+=()|^\-]{1,65}$`)

type somBridge struct {
	mu               sync.Mutex
	dir, token, peer string
}

func (c *Client) SetSOMDirectory(directory, allowedPeer string) error {
	if !filepath.IsAbs(directory) || filepath.Clean(directory) == string(filepath.Separator) {
		return errors.New("SoM bridge requires an app-private directory")
	}
	peer, err := netip.ParseAddr(allowedPeer)
	if err != nil || !peer.Is4() || !tailRange.Contains(peer) {
		return errors.New("SoM bridge requires one Tailscale IPv4 peer")
	}
	if err = os.MkdirAll(directory, 0700); err != nil {
		return err
	}
	c.som.mu.Lock()
	defer c.som.mu.Unlock()
	c.som.dir = directory
	c.som.peer = peer.String()
	if c.som.token == "" {
		path := filepath.Join(c.server.Dir, "som-token")
		raw, _ := os.ReadFile(path)
		token := strings.TrimSpace(string(raw))
		decoded, e := hex.DecodeString(token)
		if e != nil || len(decoded) != 16 {
			raw = make([]byte, 16)
			if _, e = rand.Read(raw); e != nil {
				return e
			}
			token = hex.EncodeToString(raw)
			if e = os.WriteFile(path, []byte(token+"\n"), 0600); e != nil {
				return e
			}
		}
		c.som.token = token
	}
	return nil
}

func (c *Client) somStatus() map[string]any {
	c.som.mu.Lock()
	defer c.som.mu.Unlock()
	enabled := false
	wram := false
	if c.som.dir != "" {
		_, enabledErr := os.Stat(filepath.Join(c.som.dir, "flxtr-som-ai-enabled"))
		enabled = enabledErr == nil
		info, wramErr := os.Stat(filepath.Join(c.som.dir, "flxtr-som-wram.bin"))
		wram = wramErr == nil && info.Size() == somWRAMBytes
	}
	return map[string]any{"active": c.som.dir != "" && c.som.token != "" && c.som.peer != "", "peer": c.som.peer, "token": c.som.token, "enabled": enabled, "wram": wram}
}
func (c *Client) SOMStatus() string { raw, _ := json.Marshal(c.somStatus()); return string(raw) }

func atomicText(dir, name, text string) error {
	temp, err := os.CreateTemp(dir, ".som-")
	if err != nil {
		return err
	}
	path := temp.Name()
	defer os.Remove(path)
	if err = temp.Chmod(0600); err == nil {
		_, err = temp.WriteString(text + "\n")
	}
	if err == nil {
		err = temp.Sync()
	}
	closeErr := temp.Close()
	if err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(path, filepath.Join(dir, name))
}

func (c *Client) somHTTP(w http.ResponseWriter, r *http.Request, remote string) bool {
	c.som.mu.Lock()
	dir, token, peer := c.som.dir, c.som.token, c.som.peer
	c.som.mu.Unlock()
	prefix := "/" + token + "/som/"
	if token == "" || !strings.HasPrefix(r.URL.Path, prefix) {
		return false
	}
	fail := func(code int, message string) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(code)
		_ = json.NewEncoder(w).Encode(map[string]string{"error": message})
	}
	if remote != peer || r.URL.RawQuery != "" {
		fail(403, "SoM bridge peer denied")
		return true
	}
	route := strings.TrimPrefix(r.URL.Path, prefix)
	if r.Method == "GET" && route == "status" {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(c.somStatus())
		return true
	}
	if r.Method == "GET" && route == "wram" {
		if _, err := os.Stat(filepath.Join(dir, "flxtr-som-ai-enabled")); err != nil {
			fail(409, "AI disabled")
			return true
		}
		path := filepath.Join(dir, "flxtr-som-wram.bin")
		file, err := os.Open(path)
		if err != nil {
			fail(404, "WRAM unavailable")
			return true
		}
		defer file.Close()
		info, err := file.Stat()
		if err != nil || info.Size() != somWRAMBytes {
			fail(409, "WRAM invalid")
			return true
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("Content-Length", "131072")
		_, _ = io.CopyN(w, file, somWRAMBytes)
		return true
	}
	if r.Method == "DELETE" && route == "enabled" {
		_ = os.Remove(filepath.Join(dir, "flxtr-som-ai-enabled"))
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, "{\"status\":\"stopped\"}\n")
		return true
	}
	if r.Method == "POST" && (route == "action" || route == "message") {
		limit := int64(66)
		if route == "action" {
			limit = 41
		}
		raw, err := io.ReadAll(io.LimitReader(r.Body, limit))
		text := strings.TrimSpace(string(raw))
		if err != nil || int64(len(raw)) >= limit {
			fail(400, "invalid bridge command")
			return true
		}
		name := "flxtr-som-ai-message"
		valid := somMessage.MatchString(text)
		if route == "action" {
			name = "flxtr-som-ai-action"
			valid = somActions[text]
		}
		if !valid {
			fail(400, "bridge command not allowlisted")
			return true
		}
		if err = atomicText(dir, name, text); err != nil {
			fail(500, "bridge write failed")
			return true
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, "{\"status\":\"ok\"}\n")
		return true
	}
	fail(404, "not found")
	return true
}
