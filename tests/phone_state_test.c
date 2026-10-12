#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <sys/wait.h>
#include "../src/child_process.h"
#include "../src/api_setup.h"
#include "../src/wifi_pairing.h"
#include "../src/phone_media.h"
int main(int argc,char **argv){
    char text[800];
    assert(!wifi_escape(text,sizeof(text),"a;b:c,d\\e\"f"));assert(!strcmp(text,"a\\;b\\:c\\,d\\\\e\\\"f"));
    if(argc>1&&!strcmp(argv[1],"wifi")){int ok=!wifi_payload(text,sizeof(text));memset(text,0,sizeof(text));puts(ok?"Active Wi-Fi QR payload available":"Wi-Fi payload unavailable");return 0;}
    char ip[64];(void)api_lan_ip(ip,sizeof(ip));assert(!api_setup_start("127.0.0.1",0));
    char token[160],reply[160],answer[100];assert(phone_command("join","Alice",token,sizeof(token))==200);
    assert(phone_command("join","Bob",reply,sizeof(reply))==200);
    assert(phone_command("party","start",reply,sizeof(reply))==200);
    for(int round=0;round<5;round++){
        assert(phone_party.phase==1&&phone_party.round==round);
        snprintf(answer,sizeof(answer),"%.32s:%d:%d",token,round,phone_question()->correct);
        assert(phone_command("answer",answer,reply,sizeof(reply))==200);
        assert(phone_command("answer",answer,reply,sizeof(reply))==409);
        api_setup_tick(phone_party.deadline);
        assert(phone_party.phase==2&&phone_party.players[0].score==(round+1)*100);
        api_setup_tick(phone_party.deadline);
    }
    assert(phone_party.phase==3&&phone_party.players[1].score==0);
    assert(!phone_local_media("/etc/passwd")&&!phone_local_media("phone-files/../secret"));
    api_setup_close();assert(phone_party.count==0);
    puts("PASS: quiz rounds, deadlines, score-once, room cleanup, local media boundaries and Wi-Fi escaping");return 0;
}
