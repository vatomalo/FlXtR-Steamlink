/* Native implementation of the public MegaPlay player's source response format.
 * No downloaded JavaScript is executed. Limits match the catalog URL capacity. */
#include <openssl/evp.h>
#include <openssl/hmac.h>
static int mega_decode(const char *input,unsigned char *out,size_t cap){
    size_t n=strlen(input);if(!n||n>8192||n%4==1)return -1;
    char encoded[8197];size_t padded=(n+3)&~(size_t)3;
    if(cap<padded/4*3+1)return -1;
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)input[i];
        if(!(isalnum(c)||c=='-'||c=='_'||c=='+'||c=='/'||c=='='))return -1;
        encoded[i]=c=='-'?'+':c=='_'?'/':(char)c;
    }
    for(size_t i=n;i<padded;i++)encoded[i]='=';
    encoded[padded]=0;int size=EVP_DecodeBlock(out,(unsigned char*)encoded,(int)padded);
    if(size<0)return -1;
    if(encoded[padded-1]=='=')size--;
    if(padded>1&&encoded[padded-2]=='=')size--;
    return size;
}
static void mega_encode(const unsigned char *in,int n,char *out){
    int len=EVP_EncodeBlock((unsigned char*)out,in,n);
    while(len&&out[len-1]=='=')out[--len]=0;
    for(int i=0;i<len;i++){if(out[i]=='+')out[i]='-';else if(out[i]=='/')out[i]='_';}
}
static int mega_token(char *url,size_t cap,time_t now){
    if(strstr(url,"?token=")||strstr(url,"&token="))return 1;
    const char *path=strchr(url+8,'/');if(!path)return 0;
    for(;*path;path++){
        if(*path!='/')continue;
        if(strlen(path)<67)break;
        if(path[33]!='/'||path[66]!='/')continue;
        int ok=1;for(int i=1;i<66;i++)if(i!=33&&!isxdigit((unsigned char)path[i]))ok=0;
        if(!ok)continue;
        char key[66],message[96],payload[132],signature[48];
        for(int i=0;i<65;i++)key[i]=(char)tolower((unsigned char)path[i+1]);
        key[65]=0;int n=snprintf(message,sizeof(message),"%lld|%s",(long long)now+90,key);
        const char secret[]="MpCdnT0k3n!9f2K#xQ7vL5mR8wN1pY4s";
        unsigned char digest[EVP_MAX_MD_SIZE];unsigned int size=0;
        if(!HMAC(EVP_sha256(),secret,sizeof(secret)-1,(unsigned char*)message,n,digest,&size))return 0;
        mega_encode((unsigned char*)message,n,payload);mega_encode(digest,(int)size,signature);
        size_t used=strlen(url);int added=snprintf(url+used,cap-used,"%ctoken=%s.%s",strchr(url,'?')?'&':'?',payload,signature);
        return added>0&&(size_t)added<cap-used;
    }
    return 1; /* The public player leaves paths without this key pattern alone. */
}
static int mega_media(json_object *root,char *url,size_t cap,time_t now){
    json_object *decoded=NULL,*source=field(root,"sources");
    const char *enc=string(root,"enc");
    if(*enc){
        unsigned char encrypted[6145],plain[6161];int n=mega_decode(enc,encrypted,sizeof(encrypted)),a=0,b=0;
        const unsigned char key[32]="i?LMTAx0Q6,:}50U",iv[16]="W0;27ToaUpl_P%'c";
        if(n<=0||n%16)return 0;
        EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();if(!ctx)return 0;
        int ok=EVP_DecryptInit_ex(ctx,EVP_aes_256_cbc(),NULL,key,iv)&&
            EVP_DecryptUpdate(ctx,plain,&a,encrypted,n)&&EVP_DecryptFinal_ex(ctx,plain+a,&b);
        EVP_CIPHER_CTX_free(ctx);if(!ok)return 0;
        plain[a+b]=0;json_tokener *tok=json_tokener_new_ex(16);if(!tok)return 0;
        decoded=json_tokener_parse_ex(tok,(char*)plain,a+b);
        ok=json_tokener_get_error(tok)==json_tokener_success;json_tokener_free(tok);
        if(!ok){json_object_put(decoded);return 0;}source=decoded;
    }
    const char *file=source&&json_object_is_type(source,json_type_string)?json_object_get_string(source):string(source,"file");
    int ok=!strncmp(file,"https://",8)&&strlen(file)<cap&&!strpbrk(file,"\r\n\t ")&&(strstr(file,".m3u8")||strstr(file,".mp4"));
    if(ok){strcpy(url,file);ok=mega_token(url,cap,now);}
    json_object_put(decoded);return ok;
}
static int mega_source(const char *html,const char *referer,char *url,size_t cap){
    const char *player=strstr(html,"id=\"megaplay-player\"");char id[32];
    if(!player){fprintf(stderr,"MegaPlay: player element missing\n");return 0;}
    while(player>html&&*player!='<')player--;
    if(!kiss_attr(player,"data-id",id,sizeof(id))||positive(id,0)<1){fprintf(stderr,"MegaPlay: numeric data-id missing\n");return 0;}
    char endpoint[160];snprintf(endpoint,sizeof(endpoint),"https://megaplay.buzz/stream/getSourcesNew?id=%s&platform=OTHER",id);
    Buffer response={NULL,0,65536};if(fetch_referred(endpoint,&response,referer)){fprintf(stderr,"MegaPlay: source endpoint request failed\n");free(response.data);return 0;}
    json_tokener *tok=json_tokener_new_ex(16);if(!tok){free(response.data);return 0;}
    json_object *root=json_tokener_parse_ex(tok,response.data,(int)response.length);
    int parsed=json_tokener_get_error(tok)==json_tokener_success;
    int ok=parsed&&mega_media(root,url,cap,time(NULL));
    if(ok)subtitle_manifest(root,NULL,url);
    if(!ok)fprintf(stderr,"MegaPlay: %s\n",parsed?"unsupported or invalid media response":"invalid source JSON");
    json_object_put(root);json_tokener_free(tok);free(response.data);return ok;
}
