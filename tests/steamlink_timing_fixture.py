from pathlib import Path
import sys
root=Path(sys.argv[1])
(root/"captions.srt").write_text(
    "1\n00:00:01,000 --> 00:00:02,000\nFirst timed cue\n\n"
    "2\n00:00:04,000 --> 00:00:05,000\nSecond timed cue\n")
