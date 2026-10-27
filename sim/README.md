# Simulator

Compilează nucleul portabil al firmware-ului (`firmware/components/core`: fonturi, grafică, cititorul IBV, paginare) și interfața (`firmware/components/ui`) pentru PC și scrie ca fișiere PNG ce ar arăta panoul de 240×416. Îi trebuie CMake, un compilator C și zlib.

    cmake -S sim -B sim/build && cmake --build sim/build
    sim/build/sim screens -o sim/out                                       # ecranele de pornire și de somn
    sim/build/sim pages corpus/out/pg1342.ibv -o sim/out --cover -n 20 -f tools/fonttool/out/fam/literata-16
    sim/build/sim stats corpus/out/*.ibv -f tools/fonttool/out/fam/ptserif-18   # paginează cărți întregi
    sim/build/sim stats corpus/out/*.ibv --fonts tools/fonttool/out/fam          # fiecare carte în fontul ei de pe dispozitiv (fontsel.c); --size 0-4 = treptele 1-5
    sim/build/sim compare corpus/out/pg1342.ibv --page 30 -o cmp.png tools/fonttool/out/fam/literata-16 tools/fonttool/out/fam/ptserif-18
    sim/make_card.sh                                                       # sim/out/card: corpusul și fonturile, ca pe cardul SD
    sim/build/sim ui sim/out/card -o sim/out/ui --idle --keys "m m n n N M n"   # toată interfața, pe un scenariu de apăsări
    sim/test/run.sh                                                        # testul cu imagini de referință

Fără `-f` se folosește fontul de interfață inclus. Opțiuni de paginare: `--margin`, `--indent`, `--line-gap`, `--para-gap`, `--ragged`, `--no-hyphen`, `--no-chapter-page`, `--no-keep-lines`, `--greedy` (rupere greedy în loc de încadrare totală).

`stats` verifică, pentru fiecare pagină a fiecărei cărți, că paginile se înlănțuie și avansează și cât iese cerneala în afara zonei de text (spațiile laterale ale glifelor și cursivele: 1–4 px; mai mult e eroare). Tipărește și un hash al pixelilor tuturor paginilor.

`ui` rulează interfața dispozitivului (`firmware/components/ui`) cu un ceas simulat. Scenariul e o listă de tokenuri separate prin spații: `n` `p` `m` sunt ÎNAINTE, ÎNAPOI și MENIU apăsate scurt, `N` `P` `M` aceleași apăsate lung, `i` termină lucrul de fundal (indexul paginilor), `t:SEC` lasă să treacă SEC secunde fără apăsări (până la somn), `b:MV` schimbă tensiunea bateriei. Fiecare apăsare durează o secundă pe ceasul simulat, iar cu `--idle` lucrul de fundal se termină după fiecare. După somn, apăsarea următoare trezește dispozitivul și nu ajunge la interfață, ca pe dispozitiv. `--wake` pornește ca după o trezire, `--battery MV` dă tensiunea de la început, `--perf` tipărește evenimentele PERF. Fiecare reîmprospătare a panoului devine `DIR/NNN.png`, iar `DIR/jurnal.txt` spune ce tastă a produs-o, dacă a fost completă sau parțială și dreptunghiul ei. Indexurile, pozițiile și semnele de carte se scriu în `CARD/.ibv/`, ca pe dispozitiv. Setările (NVS pe dispozitiv) se scriu în `CARD/.ibv/sim-settings`.

`test/run.sh` desenează `test/layout.ibv` în două configurații și le compară cu `test/golden/`. Cartea e făcută de `test/make_fixture.py` și are fiecare tip de înregistrare, fiecare indicator și fiecare caracter special. Apoi rulează un scenariu `ui` de 22 de reîmprospătări pe aceeași carte și îl compară cu `test/golden/ui/`, imagine cu imagine și cu tot cu jurnal. După o schimbare intenționată a paginării sau a interfeței, se privesc paginile noi și se rulează `test/run.sh --update`. Scriptul paginează și corpusul, dacă a fost convertit.

Pe dispozitiv singura diferență e `inflate_rom.c` (miniz din ROM-ul ESP32-S3) în loc de `inflate_zlib.c`.
