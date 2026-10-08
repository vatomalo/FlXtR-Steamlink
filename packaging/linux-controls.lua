local keys = {TAB='m', ESC='b', UP='u', DOWN='d', LEFT='l', RIGHT='r', ENTER='a', v='v'}
for name, command in pairs(keys) do
    mp.add_forced_key_binding(name, 'flxtr_' .. name, function()
        mp.commandv('script-message', 'flxtr', command)
    end, {repeatable=true})
end
