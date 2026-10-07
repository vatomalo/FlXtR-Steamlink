#!/usr/bin/env python3
"""Generate named WASM import shims; refuse unfamiliar imports/signatures."""
import pathlib
import re
import sys
text = pathlib.Path(sys.argv[1]).read_text()
bodies = {
    '__wbg_call:2': 'return call(a0,128);',
    '__wbg_call:3': 'return call(a0,a2);',
    '__wbg_colorDepth': 'return 24;',
    '__wbg_createElement': 'return object(CANVAS);',
    '__wbg_document': 'return object(DOCUMENT);',
    '__wbg_fillText': '',
    '__wbg_getContext': 'return object(CONTEXT);',
    '__wbg_getElementsByTagName': 'u32 id=object(ARRAY);obj(id)->a=a2==4&&!memcmp(memory(a1,a2),"body",4)?1:0;return id;',
    '__wbg_getItem': 'storage_get(a0,a2,a3);',
    '__wbg_getTimezoneOffset': 'return 0;',
    '__wbg_height': 'return 1080;',
    '__wbg_width': 'return 1920;',
    '__wbg_instanceof_CanvasRenderingContext2d': 'return obj(a0)->type==CONTEXT;',
    '__wbg_instanceof_HtmlCanvasElement': 'return obj(a0)->type==CANVAS;',
    '__wbg_instanceof_Window': 'return obj(a0)->type==WINDOW;',
    '__wbg_language': 'return_string(a0,"en-US");',
    '__wbg_length': 'return obj(a0)->a;',
    '__wbg_localStorage': 'return object(STORAGE);',
    '__wbg_navigator': 'return object(NAVIGATOR);',
    '__wbg_new0': 'return object(DATE);',
    '__wbg_new': 'return new_promise(a0,a1);',
    '__wbg_newnoargs': 'return object(FN_GLOBAL);',
    '__wbg_now:0': 'return clock_ms(0);',
    '__wbg_now:1': 'return clock_ms(1);',
    '__wbg_performance': 'return object(PERFORMANCE);',
    '__wbg_platform': 'return_string(a0,"Linux armv7l");',
    '__wbg_queueMicrotask:void': 'enqueue(a0,128,0,0);',
    '__wbg_queueMicrotask:u32': 'return object(FN_QUEUE);',
    '__wbg_random': 'return (double)rand()/((double)RAND_MAX+1.0);',
    '__wbg_reject': 'return promise(2,a0);',
    '__wbg_resolve': 'return obj(a0)->type==PROMISE?a0:promise(1,a0);',
    '__wbg_screen': 'return object(SCREEN);',
    '__wbg_setItem': 'storage_set(a1,a2,a3,a4);',
    '__wbg_setfont': '',
    '__wbg_setheight': '',
    '__wbg_settextBaseline': '',
    '__wbg_setwidth': '',
    '__wbg_static_accessor_GLOBAL': 'return object(WINDOW);',
    '__wbg_static_accessor_GLOBAL_THIS': 'return object(WINDOW);',
    '__wbg_static_accessor_SELF': 'return object(WINDOW);',
    '__wbg_static_accessor_WINDOW': 'return object(WINDOW);',
    '__wbg_then': 'u32 p=promise(0,128);enqueue(a1,128,a0,p);return p;',
    '__wbg_toDataURL': 'return_string(a0,RESOLVER_CANVAS);',
    '__wbg_userAgent': 'return_string(a0,RESOLVER_UA);',
    '__wbindgen_cb_drop': 'Object *o=obj(a0);if(o->refs--==1){o->a=0;return 1;}return 0;',
    '__wbindgen_closure_wrapper976': 'u32 id=object(CLOSURE);obj(id)->a=a0;obj(id)->b=a1;return id;',
    '__wbindgen_is_function': 'int t=obj(a0)->type;return t>=FN_GLOBAL&&t<=CLOSURE;',
    '__wbindgen_is_undefined': 'return obj(a0)->type==UNDEFINED;',
    '__wbindgen_object_clone_ref': 'return a0;',
    '__wbindgen_object_drop_ref': '',
    '__wbindgen_string_new': 'return string_object(a0,a1);',
    '__wbindgen_throw': 'char *s=copy_string(a0,a1);snprintf(error_text,sizeof(error_text),"%s",s);free(s);longjmp(failure,1);',
}
pattern = r"/\* import: 'wbg' '([^']+)' \*/\s+(u32|void|f64) (\w+)\(struct w2c_wbg\*((?:, (?:u32|f64))*)\);"
out=[]
for original, result, name, args in re.findall(pattern,text):
    types=re.findall(r'u32|f64',args)
    key=re.sub(r'_[0-9a-f]{16}$','',original)
    if key in ('__wbg_call','__wbg_now'): key+=f':{len(types)}'
    if key=='__wbg_queueMicrotask': key+=':'+result
    if key not in bodies: raise SystemExit(f'Unsupported decoder import: {original}')
    params=', '.join(['struct w2c_wbg *host']+[f'{t} a{i}' for i,t in enumerate(types)])
    unused='(void)host;'+''.join(f'(void)a{i};' for i in range(len(types)))
    out.append(f'{result} {name}({params}){{{unused}{bodies[key]}}}\n')
if len(out)!=52: raise SystemExit(f'Unexpected decoder import count: {len(out)}')
pathlib.Path(sys.argv[2]).write_text(''.join(out))
