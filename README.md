# PlumbCAD

A PlumbCAD C++20, OpenGL és Dear ImGui alapú 3D csőhálózat-szerkesztő. Kézzel vagy A* algoritmussal építhetsz csőhálózatot, ellenőrizheted a hálózati és hidraulikai feltételeket, és darabjegyzéket készíthetsz.

## Indítás és fordítás

A kész Windows-verzió a projektmappában lévő `PlumbCAD.exe` fájllal indítható. A felületről futtatható funkcionális tesztekhez a `FunkcionalisTesztek.exe` és a `PlumbCADIntegrationTests.exe` legyen a program mellett. A beépített minták a `tests/pcad` mappában vannak.

A fordításhoz C++20 fordító, CMake 3.20 vagy újabb és Ninja szükséges. Windows alatt a felületi tesztindítót a .NET-keretrendszer `csc.exe` fordítója készíti. A program és a funkcionális tesztek OpenGL 3.3 környezetet igényelnek.

Visual Studióban a **File → Open → Folder** menüből nyisd meg a projekt gyökerét. Válaszd az **x64 Debug** konfigurációt, majd a `PlumbCAD.exe` indítási célt. Parancssorból egy x64 Visual Studio fejlesztői terminálban ugyanezt a konfigurációt használhatod.

```powershell
cmake --preset x64-Debug
cmake --build out/build/x64-Debug --target PlumbCAD PlumbCADUnitTests
```

Az x64 Debug konfigurációban a PlumbCAD fordítása a funkcionális tesztfuttatókat is a program mellé helyezi. Saját konfigurációban ezt a `BUILD_TESTING=ON` és `PLUMBCAD_BUILD_INTEGRATION_TESTS=ON` kapcsolókkal engedélyezheted.

## Szerkesztés és nézet

- A felső eszköztáron egyenes csövet, 45°-os vagy 90°-os könyököt, T-idomot, szűkítőt, szelepet vagy végzárót adhatsz hozzá. Bal kattintással válassz elemet a jelenetből. Az **Objektum** panelen módosíthatod az adatait. Az egyenes cső hossza 0,04 és 8 m között állítható.
- Az **1** a mozgatás, a **2** a forgatás, a **3** a méret eszközt kapcsolja be. A jobb alsó **Szerkesztési eszközök** ablakban tengelyt választhatsz, és együtt mozgathatod vagy forgathatod a kapcsolt hálózatrészt. Mozgatás közben a **Ctrl** 10 mm-es, a **Ctrl+Shift** 1 mm-es rácsra igazít.
- Az **Objektum** panelhez tartozó **Csatlakozás / új elem** ablakban szabad csővéghez adhatsz csövet vagy idomot. Mozgatás vagy forgatás után az egymáshoz illő, 5 cm-en belüli szabad végek automatikusan kapcsolódhatnak.
- Jobb egérgombbal forgathatod a kamerát. A **W/A/S/D** mozgatja, a **Q/E** lejjebb vagy feljebb viszi, a **Shift** gyorsít. A görgővel nagyíthatsz. A **Vetület** gomb perspektivikus és ortografikus nézet között vált, a **Wireframe** a háromszögháló éleit mutatja.
- **Ctrl+Z** visszavonás, **Ctrl+Y** újra végrehajtás. A beépített **Útmutató** ugyanezeket a lépéseket a programban is összefoglalja.

## Automatikus csőnyomvonal

1. Az **Objektum** panel **Csatlakozás / új elem** ablakában jelölj ki két különböző szabad véget az **A* START** és **A* END** gombbal.
2. Az **Akadályok** panelen falat, födémet, aknát vagy tiltott zónát adhatsz hozzá. A keresés az **A* akadályként aktív** elemeket kerüli.
3. Az **Útvonal** panelen állítsd be a csőkategóriát, a rácsméretet és az akadálytávolságot, majd indítsd az **Útvonal-lehetőségek keresése** műveletet.
4. Válassz a talált lehetőségek közül, nézd meg az előnézetet, majd kattints az **Ezt az útvonalat végleg létrehozom** gombra. Az elkészült útvonal két végpontja lesz a közös validációs START és END.

A nyomvonal a választott kategória START méretéhez legközelebbi katalógusméretét használja. Ha a nyomvonal és az END csővég átmérője eltér, a program közvetlenül az END elé tervez méretváltó idomot. Ehhez a katalógusban mindkét méretre és az idom helyén szabad térre van szükség.

## Validáció és export

A **Validáció panelen** külön futtathatók a nyitott csővégek, az izolált elemek, a hálózat összefüggősége és átmérői, valamint a vízhálózati alapadatok ellenőrzései. A tervezetten szabad csővéget az **Objektum** panelen a **Szándékosan nyitott vég** kapcsolóval jelöld.

A nyomáspróba a **Validáció START** elemhez kapcsolt hálózatrészt vizsgálja. A vízsebesség, a Darcy-Weisbach nyomásveszteség és az Approved Document H 6. táblázatára épülő lefolyási ellenőrzés a közös **Validáció START** és **Validáció END** közötti útvonalon fut. A lefolyás irányát a START-tól az END felé értelmezi a program. A szükséges térfogatáramokat, nyomásértékeket és egyéb bemeneteket az adott vizsgálatnál állíthatod be.

Az **Eredmények** táblázatban a vizsgálat neve hibánál piros, figyelmeztetésnél sárga, megfelelésnél zöld. A hibák kerülnek előre, számuk a táblázat felett látható. Az **ID** kijelöli az érintett elemet. A **Részletek** alatt olvasható például a lejtéshiba oka és az elvárt lejtés.

- A **Mentés Excelhez (CSV)** a legutóbbi validációt a munkamappa `plumbcad_validation.csv` fájljába menti.
- A **Darabjegyzék exportálása CSV-be** a darabszámokat és az egyenes csőhosszakat a munkamappa `plumbcad_bom.csv` fájljába menti.
- Excelben az **Adatok → Szövegből/CSV-fájlból** importálásnál válaszd az UTF-8 kódolást és a pontosvessző elválasztót, ha az oszlopok egybeolvadnak.

## Tesztesetek és mintaprojektek

A felső **Tesztesetek** fül a 21 funkcionális eset nevét, ellenőrzési célját és eredményét mutatja. A **Funkcionális tesztek futtatása** egyben indítja az eseteket. A **Részletes jelentés** megnyitja az esetenkénti naplót és a futás során létrehozott fájlokat. Minden futás saját almappába kerül a program melletti `tesztfutasok` könyvtárban.

A **Projekt** oszlopból mind a [32 mintaprojekt](tests/pcad/README.md) elérhető futtatás előtt is. A **Betöltés** egyetlen mintát nyit meg. A **Minták…** menüben több változat közül választhatsz. Sikeres betöltés után a program a **Jelenet** panelre vált. A kiinduló A* mintán a két szabad végpontot a betöltés után jelöld ki.

Az egységtesztek Visual Studióban a **Test → Test Explorer** ablakban vagy a `PlumbCADUnitTests.exe` indítási céllal futtathatók. Parancssorból az előzőleg lefordított x64 Debug konfigurációt használhatod.

```powershell
ctest --test-dir out/build/x64-Debug -L unit --output-on-failure
ctest --test-dir out/build/x64-Debug -L integration -V
```

A CTest a funkcionális csomagot egy bejegyzésként mutatja. A részletes naplóban mind a 21 eset külön szerepel. A CTest-futás mintái a buildmappa `tests/results/pcad` almappájába kerülnek.

## Projektfájlok

A felső **Mentés** és **Betöltés** gomb az aktív `.pcad` fájlt használja. Alapértelmezésben ez a munkamappa `plumbcad_project.pcad` fájlja. Ha a Tesztesetek panelről megnyitsz egy mintát, a **Mentés** azt a fájlt írja. A **PlumbCAD.exe** indításkor megadott `.pcad` útvonalat is betölt.

A jelenlegi mentési formátum a v11. A fájl tárolja a hálózatot, az akadályokat, valamint a validációs végpontokat és beállításokat. Az A* keresés kijelöléseit és az előnézetet a program munkamenet közben kezeli.

A forráskód moduljait az [architektúra leírása](docs/architecture.md) mutatja be.
