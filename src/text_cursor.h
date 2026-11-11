/* Small UTF-8 aware insertion cursor shared by gamepad text keyboards.
 * Left/right move within the typed string, and B deletes behind the cursor. */
#ifndef FLXTR_TEXT_CURSOR_H
#define FLXTR_TEXT_CURSOR_H
static void flxtr_cursor_clamp(const char *value,size_t *cursor){
    size_t length=strlen(value);
    if(*cursor>length)*cursor=length;
    while(*cursor&&((unsigned char)value[*cursor]&0xc0)==0x80)(*cursor)--;
}
static void flxtr_cursor_left(const char *value,size_t *cursor){
    flxtr_cursor_clamp(value,cursor);
    if(!*cursor)return;
    (*cursor)--;
    while(*cursor&&((unsigned char)value[*cursor]&0xc0)==0x80)(*cursor)--;
}
static void flxtr_cursor_right(const char *value,size_t *cursor){
    flxtr_cursor_clamp(value,cursor);
    if(!value[*cursor])return;
    (*cursor)++;
    while(value[*cursor]&&((unsigned char)value[*cursor]&0xc0)==0x80)(*cursor)++;
}
static int flxtr_cursor_insert(char *value,size_t capacity,size_t *cursor,const char *input){
    flxtr_cursor_clamp(value,cursor);
    size_t len=strlen(value),extra=strlen(input);
    if(extra>capacity-1-len)return 0;
    memmove(value+*cursor+extra,value+*cursor,len-*cursor+1);
    memcpy(value+*cursor,input,extra);
    *cursor+=extra;
    return 1;
}
static void flxtr_cursor_backspace(char *value,size_t *cursor){
    flxtr_cursor_clamp(value,cursor);
    if(!*cursor)return;
    size_t end=*cursor;
    flxtr_cursor_left(value,cursor);
    memmove(value+*cursor,value+end,strlen(value+end)+1);
}
#endif
