"""Apply the small command-62 bridge to the pinned RetroArch build tree."""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
declarations = ('\n/* Steam Link queued audio: Libretro buffer-status API bridge. */\n'
                'extern bool flxtr_audio_buffer_register(void *data);\n'
                'extern void flxtr_audio_buffer_notify(void);\n')

def edit(name, changes):
    path = root / name
    text = path.read_text()
    original = text
    for before, after in changes:
        if after in text:
            continue
        if text.count(before) != 1:
            raise SystemExit(f'Unexpected pinned source: {name}: {before!r}')
        text = text.replace(before, after, 1)
    if text != original:
        path.write_text(text)

include = '#include "audio/audio_driver.h"\n'
edit('dynamic.c', [
    (include, include + declarations),
    ('      case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:',
     '      case 62: /* RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK */\n'
     '         RARCH_LOG("Environ SET_AUDIO_BUFFER_STATUS_CALLBACK.\\n");\n'
     '         return flxtr_audio_buffer_register(data);\n\n'
     '      case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:')])
edit('core_impl.c', [
    (include, include + declarations),
    ('bool core_unload(void)\n{',
     'bool core_unload(void)\n{\n   flxtr_audio_buffer_register(NULL);'),
    ('bool core_unload_game(void)\n{',
     'bool core_unload_game(void)\n{\n   flxtr_audio_buffer_register(NULL);'),
    ('   current_core.retro_run();\n\n   if (current_core.poll_type',
     '   flxtr_audio_buffer_notify();\n   current_core.retro_run();\n\n   if (current_core.poll_type'),
    ('bool core_run_no_input_polling(void)\n{',
     'bool core_run_no_input_polling(void)\n{\n   flxtr_audio_buffer_notify();')])

