/* profile.c - investor profile: risk score, saving and loading. */
#include "model.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *goal_kind_name(int k) {
    static const char *n[] = { "Buy a house", "Education", "Retirement", "Buy a car", "Travel", "Grow wealth" };
    return k >= 0 && k < G_KINDS ? n[k] : "Goal";
}

const char *risk_label(float r) {
    if (r < 0.30f) return "Careful";
    if (r < 0.50f) return "Balanced";
    if (r < 0.70f) return "Growth";
    return "Bold";
}

/* Risk comfort = 55% questionnaire + 25% age + 20% time until the nearest goal.
 * Younger investors with far-away goals can ride out falls; someone who needs
 * the money in two years cannot, however brave they feel. */
void profile_compute_risk(Profile *p) {
    float q = 0;
    for (int i = 0; i < 4; i++) q += p->quiz[i];
    q /= 8.0f;
    float age = (60.0f - (p->age < 18 ? 18 : p->age > 60 ? 60 : p->age)) / 42.0f;
    int h = 30;
    for (int i = 0; i < p->ngoals; i++) if (p->goals[i].years < h) h = p->goals[i].years;
    if (!p->ngoals) h = 5;
    float hz = h >= 10 ? 1.0f : (h - 1) / 9.0f;
    if (hz < 0) hz = 0;
    p->risk = 0.55f * q + 0.25f * age + 0.20f * hz;
    if (p->risk < 0) p->risk = 0;
    if (p->risk > 1) p->risk = 1;
}

/* each signed-in user has their own profile file */
static char pfile[64] = "profile.txt";
void profile_set_file(const char *name) { snprintf(pfile, sizeof pfile, "%s", name && name[0] ? name : "profile.txt"); }
bool profile_save(const Profile *p, const char *dir) {
    char path[400]; snprintf(path, sizeof path, "%.300s/%s", dir, pfile);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "name=%s\nage=%d\nlump=%lld\nsip=%lld\nquiz=%d,%d,%d,%d\nmarkets=%d\n", p->name, p->age, p->lump, p->sip,
            p->quiz[0], p->quiz[1], p->quiz[2], p->quiz[3], p->markets);
    for (int i = 0; i < p->ngoals; i++) fprintf(f, "goal=%d|%lld|%d|%s\n", p->goals[i].kind, p->goals[i].target, p->goals[i].years, p->goals[i].name);
    fclose(f);
    return true;
}

bool profile_load(Profile *p, const char *dir) {
    char path[400], line[256]; snprintf(path, sizeof path, "%.300s/%s", dir, pfile);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    memset(p, 0, sizeof *p);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '='); if (!v) continue; *v++ = 0;
        if (!strcmp(line, "name")) snprintf(p->name, sizeof p->name, "%s", v);
        else if (!strcmp(line, "age")) p->age = atoi(v);
        else if (!strcmp(line, "lump")) p->lump = atoll(v);
        else if (!strcmp(line, "sip")) p->sip = atoll(v);
        else if (!strcmp(line, "quiz")) sscanf(v, "%d,%d,%d,%d", &p->quiz[0], &p->quiz[1], &p->quiz[2], &p->quiz[3]);
        else if (!strcmp(line, "markets")) p->markets = atoi(v);
        else if (!strcmp(line, "goal") && p->ngoals < MAX_GOALS) {
            Goal *g = &p->goals[p->ngoals];
            char name[64] = "";
            if (sscanf(v, "%d|%lld|%d|%63[^\n]", &g->kind, &g->target, &g->years, name) >= 3) { snprintf(g->name, sizeof g->name, "%s", name); p->ngoals++; }
        }
    }
    fclose(f);
    p->done = p->age > 0 && (p->lump > 0 || p->sip > 0);
    if (!p->markets) p->markets = 3;
    profile_compute_risk(p);
    return p->done;
}
