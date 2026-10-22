#include "../src/service_login.h"
#include <assert.h>
#include <unistd.h>
int main(void){
    const char *sub="https://api.subdl.com/api/v1/subtitles?apikey=secret";
    const char *games="https://api.thegamesdb.net/v1.1/Games/ByGameName";
    unlink(service_login_path("subdl"));unlink(service_login_path("gamesdb"));
    assert(!service_login_response("https://example.com/",401,NULL));
    assert(!service_login_response("https://api.thegamesdb.net.evil/",401,NULL));
    assert(!service_login_response(sub,403,"Forbidden"));
    assert(!service_login_response(sub,429,"Quota exceeded"));
    assert(!service_login_response(sub,0,NULL));
    assert(!service_login_response(sub,200,"{\"status\":true}"));
    assert(!service_login_response(games,200,"{\"data\":{\"overview\":\"Unauthorized: Invalid API key\"}}"));
    assert(access(service_login_path("subdl"),F_OK));
    assert(service_login_response(sub,401,NULL));
    FILE *f=fopen(service_login_path("subdl"),"r");assert(f);assert(fgetc(f)==EOF);fclose(f);
    assert(service_login_response(games,403,"{\"message\":\"Invalid API key\"}"));
    assert(!access(service_login_path("gamesdb"),F_OK));
    unlink(service_login_path("gamesdb"));unlink(service_login_path("subdl"));
    assert(service_login_response(sub,200,"{\"status\":false,\"error\":\"API key expired\"}"));
    unlink(service_login_path("subdl"));
    assert(!service_login_path("unknown"));
    puts("PASS: authentication detection, provider boundaries and credential-free notifications");
}
