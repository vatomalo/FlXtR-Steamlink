"""Ollama model selection for the Secret of Mana bridge (SkyLoft side).

The console menu stores a numeric model index and critic bit in som_ai_brain.
Only this fixed allowlist reaches Ollama. Pulling runs off the gameplay thread.
"""
import json
import queue
import threading
import urllib.request

# Keep these labels in the exact order of som_ai_model_names in src/som_gambit.h.
AI_MODELS = (
    ("Qwen3 0.6B", "qwen3:0.6b"),
    ("Qwen3 1.7B", "qwen3:1.7b"),
    ("Qwen3 4B", "qwen3:4b"),
    ("Qwen3.5 4B", "qwen3.5:4b"),
    ("Qwen2.5 3B", "qwen2.5:3b"),
    ("StarCoder2 3B", "starcoder2:3b"),
    ("Llama3.2 1B", "llama3.2:1b"),
    ("Llama3.2 3B", "llama3.2:3b"),
    ("Gemma3 1B", "gemma3:1b"),
    ("Gemma3 4B", "gemma3:4b"),
    ("Phi-4 mini", "phi4-mini:3.8b"),
    ("SmolLM2 1.7B", "smollm2:1.7b"),
    ("DeepSeek-R1 1.5B", "deepseek-r1:1.5b"),
    ("Mistral 7B", "mistral:7b"),
    ("Granite3.3 2B", "granite3.3:2b"),
)
DEFAULT_BRAIN = (0, True)


def parse_brain(text):
    """Strict wire-format parse: model 0..14, critic 0/1; reject extra tokens."""
    if not isinstance(text, str):
        return DEFAULT_BRAIN
    fields = text.strip().split()
    if len(fields) != 2 or not fields[0].isascii() or not fields[0].isdigit():
        return DEFAULT_BRAIN
    if fields[1] not in ("0", "1"):
        return DEFAULT_BRAIN
    index = int(fields[0])
    return (index, fields[1] == "1") if 0 <= index < len(AI_MODELS) else DEFAULT_BRAIN


def _request(url, path, body=None, timeout=30):
    if url.rstrip("/") not in ("http://127.0.0.1:11434", "http://localhost:11434"):
        raise ValueError("Only local Ollama is allowed")
    data = json.dumps(body).encode("utf-8") if body is not None else None
    return urllib.request.Request(
        url.rstrip("/") + path, data=data,
        headers={"Content-Type": "application/json"} if data is not None else {})


def ensure_model(url, tag, progress=lambda _: None, cancelled=lambda: False):
    """Download a missing allowlisted Ollama model, with streamed progress.

    Return False if the user selected another model before this pull completed.
    Never switch the active director before a complete, successful pull.
    """
    if tag not in {model for _, model in AI_MODELS}:
        raise ValueError("Refusing non-allowlisted model")
    with urllib.request.urlopen(_request(url, "/api/tags"), timeout=15) as response:
        tags = json.load(response).get("models", [])
    installed = {str(item.get("name", "")) for item in tags}
    if tag in installed or (tag.endswith(":latest") and tag[:-7] in installed):
        return True
    progress("Downloading")
    last_percent = -1
    with urllib.request.urlopen(
            _request(url, "/api/pull", {"model": tag, "stream": True}),
            timeout=900) as response:
        finished = False
        for raw in response:
            if cancelled():
                return False
            reply = json.loads(raw)
            if reply.get("error"):
                raise OSError(str(reply["error"])[:160])
            total = reply.get("total") or 0
            completed = reply.get("completed") or 0
            if total:
                pct = max(0, min(100, int(100 * completed / total)))
                bucket = pct // 10
                if bucket > last_percent:
                    last_percent = bucket
                    progress(f"{pct}%")
            if reply.get("status") == "success":
                finished = True
        if not finished:
            raise OSError("Ollama pull ended without success")
    return not cancelled()


class BrainController:
    """One downloader maximum; latest selection wins, events drained in main loop."""

    def __init__(self, url, initial_model="qwen3:0.6b"):
        self.url = url
        self.model = initial_model
        self.critic = True
        self.desired = None
        self._running = False
        self._lock = threading.Lock()
        self._events = queue.Queue()

    def select(self, index, critic):
        if not 0 <= index < len(AI_MODELS):
            index, critic = DEFAULT_BRAIN
        self.critic = bool(critic)
        with self._lock:
            if index == self.desired:
                return
            self.desired = index
            if not self._running:
                self._running = True
                threading.Thread(target=self._work, daemon=True,
                                 name="som-ollama-model-pull").start()

    def _work(self):
        while True:
            with self._lock:
                selected = self.desired
            tag = AI_MODELS[selected][1]
            try:
                ok = ensure_model(
                    self.url, tag,
                    progress=lambda message: self._events.put(("progress", selected, message)),
                    cancelled=lambda: self.desired != selected)
                if ok:
                    self._events.put(("ready", selected, tag))
            except (OSError, ValueError, TimeoutError) as exc:
                self._events.put(("error", selected, str(exc)[:80]))
                with self._lock:
                    if self.desired == selected:
                        self._running = False
                        return
            with self._lock:
                if self.desired == selected:
                    self._running = False
                    return

    def drain(self):
        """Return UI messages, applying a ready model only if still selected."""
        messages = []
        while True:
            try:
                kind, index, detail = self._events.get_nowait()
            except queue.Empty:
                break
            if index != self.desired:
                continue
            name = AI_MODELS[index][0]
            if kind == "ready":
                self.model = detail
                messages.append("AI ready|" + name)
            elif kind == "error":
                messages.append("AI download failed|" + name)
                print(f"[ai-brain] {name}: {detail}", flush=True)
            elif kind == "progress":
                messages.append(f"AI {detail}|{name}")
        return messages
