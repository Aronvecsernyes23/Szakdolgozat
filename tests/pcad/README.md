# Funkcionális tesztprojektek

A fájlnevek első két számjegye a funkcionális futtató esetszáma. A betű az ugyanahhoz az esethez tartozó bemenetet, eredményt vagy paraméterváltozatot jelöli.

## Megnyitás

A PlumbCAD felső eszköztárán nyisd meg a **Tesztesetek** panelt. A **Projekt** oszlopban a **Betöltés** közvetlenül megnyitja a mintát, a **Minták…** menüben pedig kiválaszthatod a kívánt változatot.

Húzd a kívánt fájlt a projekt gyökerében lévő `PlumbCAD.exe`-re, vagy a projekt gyökerében megnyitott PowerShellben futtasd például ezt:

```powershell
.\PlumbCAD.exe .\tests\pcad\17b_astar_end_50_32_eredmeny.pcad
```

A program induláskor betölti a megadott fájlt. Ha a példát szerkesztenéd és mentenéd, előbb készíts róla másolatot, mert a **Mentés** a megnyitott projektfájlba ír.

## Tesztesetek

| Eset | Mit lehet megvizsgálni? |
| --- | --- |
| 01 | Automatikus illesztés 60 mm-es résnél, 30 mm-es és szögben eltérő helyzetben, valamint a sikeresen illesztett eredmény. |
| 02 | Az 1-es cső egyik szabad vége jelöletlen, a másik szándékosan nyitott. |
| 03 | Az 1-es cső mindkét vége szándékosan nyitott. |
| 04 | Közvetlenül összekapcsolt Ø110 és Ø75 cső. A hálózati validáció átmérőeltérést jelez. |
| 05 | Ugyanez a csatlakozás Ø110/75 szűkítővel. Az átmérő-ellenőrzés megfelel. |
| 06 | Falas A* keresés bemenete és az elkészült PVC-U kerülőút. |
| 07 | PPR végpontok között létrehozott PVC-U Ø75 nyomvonal. |
| 08 | PVC-U Ø75 cső 1:100 lejtéssel, 0,50 l/s térfogatárammal. A lejtés túl kicsi. |
| 09 | PVC-U Ø75 cső 1:50 lejtéssel, 3,00 l/s térfogatárammal. A kapacitás 2,80 l/s. |
| 10 | Cső és szelep 0,0308 bar teljes veszteséggel. A megadott határ 0,10, illetve 0,02 bar. |
| 11 | Két összekapcsolt cső, fal, szándékosan nyitott vég, kijelölt validációs végpontok és elmentett beállítások. |
| 12 | Három egyenes cső és egy könyök. A darabjegyzékben három csoport és 5,50 m egyenes csőhossz szerepel. |
| 13 | Nyomáspróba 0,10 bar, illetve 0,40 bar nyomáseséssel, 0,20 bar megengedett esés mellett. |
| 14 | A három sebességminősítés 1, 2 és 3 l/s térfogatárammal. |
| 15 | PVC-U Ø75 cső 1:30 lejtéssel, 0,50 l/s térfogatárammal. A lefolyási vizsgálat megfelel. |
| 16 | PVC-U Ø75 cső, amely a START-tól az END felé emelkedik. |
| 17 | PPR Ø50 → Ø32. Bemenet és az END-nél elhelyezett szűkítővel megépített útvonal. |
| 18 | PPR Ø32 → Ø50. Bemenet és az END-nél elhelyezett átmenettel megépített útvonal. |
| 19 | PPR Ø50 START és réz Ø54 END. Bemenet és a szűkítő nélküli réz nyomvonal. |
| 20 | Egy fal az END-szűkítő helyén. Az útvonalkeresés az ütközést jelzi. |
| 21 | Két összekötött cső, nyitott vég és sebességfigyelmeztetés a validációs CSV-export kipróbálásához. A pontosvesszős, idézőjeles és többsoros mezőket a C++ teszt külön, előállított üzeneteken ellenőrzi. |

A validációs minták betöltése után futtasd a megfelelő vizsgálatot a **Validáció panelen**. Egy mintában egyszerre több ellenőrzés is adhat eredményt. A táblázat az adott esethez tartozó vizsgálatot nevezi meg.

Az A* bemeneteknél a csatlakozási ablakban jelöld meg az **1-es elem 1-es végpontját** A* START-ként, a **2-es elem 0-s végpontját** A* END-ként. A v11 formátum az A* kijelöléseket és a keresés beállításait nem tárolja. Az **Útvonal** panelen használj 0,35 m rácsméretet és 0,10 m akadálytávolságot. A 06–07. esetben PVC-U, a 17–18. és 20. esetben PPR, a 19. esetben réz legyen az útvonal anyaga.

A funkcionális futtató a saját kimeneti könyvtárának `pcad` almappájában állítja elő ugyanezeket a mintákat. A projektben lévő példányokat a tesztfuttatás nem írja felül.
