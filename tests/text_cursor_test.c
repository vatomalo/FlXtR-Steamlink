#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/text_cursor.h"
int main(void){
    char query[32]="ABCDE";
    size_t cursor=2;
    flxtr_cursor_left(query,&cursor);
    assert(cursor==1&&!strcmp(query,"ABCDE"));
    flxtr_cursor_right(query,&cursor);
    assert(cursor==2&&!strcmp(query,"ABCDE"));
    assert(flxtr_cursor_insert(query,sizeof(query),&cursor," "));
    assert(!strcmp(query,"AB CDE")&&cursor==3);
    flxtr_cursor_backspace(query,&cursor);
    assert(!strcmp(query,"ABCDE")&&cursor==2);
    strcpy(query,"A\xc3\xa9Z");cursor=strlen(query);
    flxtr_cursor_left(query,&cursor);assert(cursor==3);
    flxtr_cursor_left(query,&cursor);assert(cursor==1);
    flxtr_cursor_right(query,&cursor);assert(cursor==3);
    flxtr_cursor_backspace(query,&cursor);assert(!strcmp(query,"AZ")&&cursor==1);
    puts("PASS: non-destructive left/right, insertion, backspace and UTF-8 cursor boundaries");
    return 0;
}
