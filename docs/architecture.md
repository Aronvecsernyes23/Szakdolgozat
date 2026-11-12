# Architektúra

A projekt a szakdolgozati célhoz igazodva modulokra bontott CAD alap:

```text
Application
 ├─ Graphics / OpenGL renderer
 ├─ UI / ImGui panelek
 ├─ Geometry / transzformáció és mesh generálás
 ├─ Pipeline / csőhálózati domain modell
 └─ Validation / ellenőrzési szabályok
```

## Miért így?

A grafikus megjelenítés nem keveredik a csőhálózati logikával. Egy csőelem a `Pipeline`
modulban műszaki adatként létezik, a megjelenítéshez szükséges hengermesh pedig a
`Geometry` modulból jön létre, és a `Graphics` modul tölti fel OpenGL pufferbe.

Ez a bontás később lehetővé teszi, hogy ugyanarra a csőhálózati modellre többféle
megjelenítést, ellenőrzést vagy exportálást építs.


## Validációs UI

A validációs felület külön `UI/ValidationPanel.cpp` modulban van. Az `EditorUi`
a panelváltást és az objektumszerkesztést kezeli, míg a validációs modul csak a
validációs beállításokat, futtatógombokat és eredménytáblát rajzolja. A számítási
logika továbbra is a `Validation` modulban marad.
