#ifndef STORE_H
#define STORE_H

// A key/value store: open a Store, then get and put entries.
typedef struct Store Store;

Store *store_open(const char *path);
void store_close(Store *s);
int store_get(Store *s, int key);
void store_put(Store *s, int key, int value);

#endif
