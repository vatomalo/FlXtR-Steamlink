local keys = {TAB='m', ESC='b', UP='u', DOWN='d', LEFT='l', RIGHT='r', ENTER='a', v='v', SPACE='P', k='P', j='R', l='F', f='t', m='o', ['Ctrl+q']='q'}
for name, command in pairs(keys) do
    mp.add_forced_key_binding(name, 'flxtr_' .. name, function()
        mp.commandv('script-message', 'flxtr', command)
    end, {repeatable=({LEFT=true, RIGHT=true, UP=true, DOWN=true, j=true, l=true})[name] or false})
end

mp.add_forced_key_binding('F1', 'flxtr_keyboard_help', function()
    mp.commandv('show-text', 'FlXtR: Space/K pause | J/L or arrows seek | F fullscreen | M mute\nTab menu | Esc stop | Ctrl+Q quit', 8000)
end)
