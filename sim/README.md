# Simulator

Compilează nucleul portabil al firmware-ului (`firmware/components/core`: fonturi, grafică, cititorul IBV, paginare) pentru PC și scrie ca fișiere PNG ce ar arăta panoul de 240×416. Îi trebuie CMake, un compilator C și zlib.

    cmake -S sim -B sim/build && cmake --build sim/build
    sim/build/sim screens -o sim/out                                       # ecranele de pornire și de somn
    sim/build/sim pages corpus/out/pg1342.ibv -o sim/out --cover -n 20 -f tools/fonttool/out/fam/literata-16
    sim/build/sim stats corpus/out/*.ibv -f tools/fonttool/out/fam/ptserif-18   # paginează cărți întregi
    sim/build/sim stats corpus/out/*.ibv --fonts tools/fonttool/out/fam          # fiecare carte în fontul ei de pe dispozitiv (fontsel.c); --size 0-4 = treptele 1-5
    sim/build/sim compare corpus/out/pg1342.ibv --page 30 -o cmp.png tools/fonttool/out/fam/literata-16 tools/fonttool/out/fam/ptserif-18
    sim/test/run.sh                                                        # testul cu imagini de referință

Fără `-f` se folosește fontul de interfață inclus. Opțiuni de paginare: `--margin`, `--indent`, `--line-gap`, `--para-gap`, `--ragged`, `--no-hyphen`, `--no-chapter-page`, `--no-keep-lines`, `--greedy` (rupere greedy în loc de încadrare totală).

`stats` verifică, pentru fiecare pagină a fiecărei cărți, că paginile se înlănțuie și avansează și cât iese cerneala în afara zonei de text (spațiile laterale ale glifelor și cursivele: 1–4 px; mai mult e eroare). Tipărește și un hash al pixelilor tuturor paginilor.

`test/run.sh` desenează `test/layout.ibv` în două configurații și le compară cu `test/golden/`. Cartea e făcută de `test/make_fixture.py` și are fiecare tip de înregistrare, fiecare indicator și fiecare caracter special. După o schimbare intenționată a paginării, se privesc paginile noi și se rulează `test/run.sh --update`. Scriptul paginează și corpusul, dacă a fost convertit.

Pe dispozitiv singura diferență e `inflate_rom.c` (miniz din ROM-ul ESP32-S3) în loc de `inflate_zlib.c`.
