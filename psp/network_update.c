#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include <cjson/cJSON.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "update_validation.h"
#define BASE "https://github.com/vatomalo/FlXtR-Steamlink/releases/download/"
int check_update(void){
    unsigned char manifest[2048];size_t used=0;
    status("Checking PSP beta updates...");
    if(transfer(BASE "psp-latest/psp.json",NULL,manifest,sizeof(manifest)-1,&used,NULL)<0)return -1;
    manifest[used]=0;cJSON *m=cJSON_Parse((char*)manifest);if(!m)return -1;
    cJSON *platform=cJSON_GetObjectItemCaseSensitive(m,"platform"),*version=cJSON_GetObjectItemCaseSensitive(m,"version");
    cJSON *sha=cJSON_GetObjectItemCaseSensitive(m,"sha256"),*build=cJSON_GetObjectItemCaseSensitive(m,"build");
    if(!cJSON_IsString(platform)||strcmp(platform->valuestring,"psp")||!cJSON_IsString(version)||
       !hex_string(version->valuestring,40)||!cJSON_IsString(sha)||!hex_string(sha->valuestring,64)||
       !cJSON_IsNumber(build)||build->valuedouble<1||build->valuedouble>4294967295.0){cJSON_Delete(m);return -1;}
    if(build->valuedouble<=FLXTR_BUILD || !strcmp(version->valuestring,FLXTR_VERSION)){
        cJSON_Delete(m);status("PSP beta is up to date");return 0;
    }
    char url[256];snprintf(url,sizeof(url),BASE "psp-%s/EBOOT.PBP",version->valuestring);
    unsigned char *data=malloc(PSP_UPDATE_MAX);if(!data){cJSON_Delete(m);return -1;}
    status("Downloading PSP update. Circle cancels.");
    int result=-1;
    if(transfer(url,NULL,data,PSP_UPDATE_MAX,&used,NULL)==0 && valid_psp_pbp(data,used)){
        unsigned char digest[32];char hex[65];
        if(mbedtls_sha256_ret(data,used,digest,0)!=0)goto done;
        for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
        if(strcmp(hex,sha->valuestring)){status("Update checksum mismatch; keeping installed app");goto done;}
        FILE *f=fopen("EBOOT.NEW","wb");if(!f)goto done;
        bool ok=fwrite(data,1,used,f)==used;
        if(fflush(f)!=0)ok=false;
        if(fsync(fileno(f))!=0)ok=false;
        if(fclose(f)!=0)ok=false;
        if(!ok){remove("EBOOT.NEW");goto done;}
        /* FAT has no atomic exchange: preserve the previous PBP for USB recovery. */
        remove("EBOOT.OLD");
        if(rename("EBOOT.PBP","EBOOT.OLD")!=0){remove("EBOOT.NEW");goto done;}
        if(rename("EBOOT.NEW","EBOOT.PBP")!=0){
            if(rename("EBOOT.OLD","EBOOT.PBP")!=0){
                status("Restore EBOOT.OLD to EBOOT.PBP via USB before exit.");result=-2;
            }
            goto done;
        }
        status("Update installed. Exit with HOME, then relaunch.");result=1;
    }
done:
    free(data);cJSON_Delete(m);
    if(result==-1)status("Update failed; existing app retained");
    return result;
}
