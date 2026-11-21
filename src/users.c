/*
 * users.c - local sign-in.
 *
 * Each person on the laptop gets an account: name -> their own profile file.
 * Names are looked up with the HASH TABLE. The PIN is never stored: we keep a
 * random salt and a stretched hash (FNV-1a, 20,000 rounds). A real product
 * would use bcrypt/Argon2 and a server; this keeps it self-contained in C.
 */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

unsigned long long pin_hash(unsigned long long salt, const char *pin) {
    unsigned long long h = 1469598103934665603ULL ^ salt;
    for (int r = 0; r < 20000; r++) {
        for (const char *p = pin; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
        h ^= (unsigned long long)r; h *= 1099511628211ULL;
        h ^= h >> 29;
    }
    return h;
}

static void lower(char *o, const char *s, int n) { int k = 0; for (; s[k] && k < n - 1; k++) o[k] = (char)tolower((unsigned char)s[k]); o[k] = 0; }

void users_load(Users *us, const char *dir) {
    memset(us, 0, sizeof *us); hm_init(&us->map);
    char p[400], line[300]; snprintf(p, sizeof p, "%.300s/users.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f) && us->n < MAX_USERS) {
        User *u = &us->u[us->n];
        if (sscanf(line, "%31[^|]|%63[^|]|%llx|%llx", u->name, u->file, &u->salt, &u->hash) == 4) {
            char k[32]; lower(k, u->name, sizeof k); hm_put(&us->map, k, us->n); us->n++;
        }
    }
    fclose(f);
}

bool users_save(const Users *us, const char *dir) {
    char p[400]; snprintf(p, sizeof p, "%.300s/users.txt", dir);
    FILE *f = fopen(p, "w");
    if (!f) return false;
    for (int i = 0; i < us->n; i++) fprintf(f, "%s|%s|%llx|%llx\n", us->u[i].name, us->u[i].file, us->u[i].salt, us->u[i].hash);
    fclose(f);
    return true;
}

int users_find(Users *us, const char *name) { char k[32]; lower(k, name, sizeof k); return hm_get(&us->map, k); }

int users_add(Users *us, const char *name, const char *pin) {
    if (!name[0] || us->n >= MAX_USERS || users_find(us, name) >= 0) return -1;
    User *u = &us->u[us->n];
    snprintf(u->name, sizeof u->name, "%s", name);
    for (char *c = u->name; *c; c++) if (*c == '|') *c = ' ';
    snprintf(u->file, sizeof u->file, "profile_%d.txt", us->n + 1);
    u->salt = ((unsigned long long)time(NULL) << 20) ^ (unsigned long long)rand() ^ ((unsigned long long)(size_t)u << 7);
    u->hash = pin && pin[0] ? pin_hash(u->salt, pin) : 0;
    char k[32]; lower(k, u->name, sizeof k); hm_put(&us->map, k, us->n);
    return us->n++;
}

bool users_check(const Users *us, int i, const char *pin) {
    if (i < 0 || i >= us->n) return false;
    if (!us->u[i].hash) return true;                      /* account made before PINs existed */
    return pin_hash(us->u[i].salt, pin) == us->u[i].hash;
}
