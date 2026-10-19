/* Record the actual game launch arguments; no emulator or ROM code executes. */
#include <stdio.h>
#include "../src/windows_runtime.h"
static int fixture(int argc,char **argv){
    FILE *out=fopen("retroarch-args.txt","wb");if(!out)return 1;
    for(int i=0;i<argc;i++)fprintf(out,"%s\n",argv[i]);
    return fclose(out)?1:0;
}
#ifdef _WIN32
FLXTR_WINDOWS_CONSOLE_ENTRY(fixture)
#else
int main(int argc,char **argv){return fixture(argc,argv);}
#endif
