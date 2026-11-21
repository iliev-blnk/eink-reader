#!/usr/bin/env python3
"""Reverifică referințele din referinte.bib.

- intrările cu `doi`: metadatele din Crossref (titlu, an, numele de familie
  ale autorilor) trebuie să se potrivească cu intrarea;
- intrările cu `url` (sau un \\url{...} în `note`): adresa trebuie să răspundă
  cu HTTP 200.

Rulare: python3 thesis/verifica_surse.py   (are nevoie de rețea)
Iese cu cod 1 dacă vreo intrare nu trece.
"""
import json
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.request
from pathlib import Path

BIB = Path(__file__).with_name("referinte.bib")
UA = "Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0"


def intrari(text):
    """Întoarce [(cheie, {câmp: valoare})] dintr-un .bib simplu."""
    rez = []
    for m in re.finditer(r"@\w+\{([^,]+),(.*?)\n\}", text, re.S):
        campuri = {}
        for c in re.finditer(r"(\w+)\s*=\s*\{(.*?)\}\s*,?\s*$", m.group(2), re.M):
            campuri[c.group(1).lower()] = c.group(2)
        rez.append((m.group(1).strip(), campuri))
    return rez


def simplu(s):
    s = unicodedata.normalize("NFKD", s)
    s = "".join(ch for ch in s if not unicodedata.combining(ch))
    return re.sub(r"[^a-z0-9]", "", s.lower())


def get(url, incercari=3):
    """GET; la HTTP 429 (prea multe cereri) așteaptă cât cere serverul și reîncearcă."""
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    for i in range(incercari):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            if e.code != 429 or i == incercari - 1:
                raise
            time.sleep(min(int(e.headers.get("Retry-After") or 30), 120))


def verifica_doi(cheie, c):
    _, corp = get("https://api.crossref.org/works/" + c["doi"])
    m = json.loads(corp)["message"]
    erori = []
    titlu = (m.get("title") or [""])[0]
    if simplu(c["title"]) not in simplu(titlu) and simplu(titlu) not in simplu(c["title"]):
        erori.append(f"titlu: Crossref are „{titlu}”")
    an = (m.get("issued") or {}).get("date-parts", [[None]])[0][0]
    if "year" in c and str(an) != c["year"]:
        erori.append(f"an: Crossref are {an}")
    if "author" in c and m.get("author"):
        ale_noastre = [simplu(a.split(",")[0]) for a in c["author"].split(" and ")]
        crossref = [simplu(a.get("family", a.get("name", ""))) for a in m["author"]]
        if ale_noastre != crossref:
            erori.append(f"autori: Crossref are {[a.get('family') for a in m['author']]}")
    return erori


def main():
    esecuri = 0
    for cheie, c in intrari(BIB.read_text(encoding="utf-8")):
        erori = []
        try:
            if "doi" in c:
                erori += verifica_doi(cheie, c)
            urluri = [c["url"]] if "url" in c else []
            urluri += re.findall(r"\\url\{([^}]+)\}", c.get("note", ""))
            for u in urluri:
                stare, _ = get(u)
                if stare != 200:
                    erori.append(f"{u}: HTTP {stare}")
        except Exception as e:  # rețea, 404, JSON stricat
            erori.append(str(e))
        print(("EȘEC " if erori else "ok   ") + cheie)
        for e in erori:
            print("       " + e)
        esecuri += bool(erori)
    print(f"{esecuri} intrări cu probleme")
    sys.exit(1 if esecuri else 0)


if __name__ == "__main__":
    main()
