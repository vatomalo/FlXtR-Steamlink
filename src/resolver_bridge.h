#ifndef RESOLVER_BRIDGE_H
#define RESOLVER_BRIDGE_H
int resolver_init(void);
char *resolver_key(void);
char *resolver_decode(const char *payload,const char *key);
void resolver_free(void);
const char *resolver_error(void);
#define RESOLVER_UA "FlXtR Steamlink native protocol research"
#define RESOLVER_CANVAS "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg=="
#endif
