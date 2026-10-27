// Jurnalul de măsurători (FW-13): marcaje de timp scrise pe card ca CSV, pentru
// experimentele E1, E2 și E4 (tools/perf/perf.py le analizează).
//
// Pornește doar dacă pe card există dosarul /perf: fiecare pornire scrie
// atunci fișierul următor, perf/0001.csv, perf/0002.csv… Rândurile sunt
// `t_us,event,a,b`. Evenimentele stau întâi în memorie și ajung pe card doar
// când platforma cheamă perflog_flush(), adică atunci când așteaptă o apăsare,
// ca scrierea pe card să nu intre în timpii măsurați. Și scrierea apare în
// jurnal (`perf_flush`, câte rânduri și cât a durat), ca să se vadă dacă tot
// s-a suprapus cu ceva.
//
// C portabil: îl folosesc și firmware-ul, și simulatorul.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Caută <root>/perf și alege fișierul. false: jurnalul rămâne oprit.
bool perflog_open(const char *root, uint64_t now_us);
bool perflog_on(void);
// Numele fișierului, față de rădăcina cardului („perf/0003.csv”), sau NULL.
const char *perflog_file(void);
// `event` trebuie să fie un șir care trăiește cât programul (un literal):
// se păstrează doar pointerul până la scriere.
void perflog_add(uint64_t t_us, const char *event, uint32_t a, uint32_t b);
// Câte rânduri așteaptă și dacă memoria e aproape plină (atunci se scrie
// chiar dacă mai e lucru de fundal).
int perflog_pending(void);
bool perflog_urgent(void);
// Scrie ce s-a adunat; `now_us` e ceasul platformei, pentru `perf_flush`.
void perflog_flush(uint64_t (*now_us)(void));
