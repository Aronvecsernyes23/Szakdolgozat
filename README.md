# PlumbCAD

PlumbCAD egy C++ / OpenGL / Dear ImGui alapú szakdolgozati prototípus vízvezeték-hálózatok egyszerű 3D szerkesztéséhez.

## Fő funkciók

- egyenes csövek, 45°/90° könyökök és T-idomok létrehozása
- csővégek automatikus illesztése és összekapcsolása
- kapcsolt részhálózat együtt mozgatása
- Perspektivikus és ortografikus térbeli nézet
- projekt mentése és betöltése
- Ctrl+Z / Ctrl+Y visszavonás és újra végrehajtás
- külön futtatható validációs ellenőrzések
- MSZ EN 806 prototípus-jellegű adat-, nyomáspróba- és áramlási sebesség ellenőrzések

## Build

Szükséges:

- CMake 3.20+
- C++20 fordító
- Visual Studio 2022 ajánlott

Build lépések:

```bash
cmake -S . -B out/build/x64-Debug
cmake --build out/build/x64-Debug
```

Visual Studio / CMake Tools használatakor:

1. Delete Cache and Reconfigure
2. Build

## Tesztek

Visual Studióban nyisd meg a projektmappát a **File → Open → Folder** menüből. Az **x64 Debug** CMake-konfiguráció engedélyezi az egységteszteket. A konfigurálás után az indítási célok között válaszd a **PlumbCADUnitTests.exe**-t, majd nyomd meg a **Ctrl+F5**-öt. Az egyes teszteket fordítás után a **Test → Test Explorer** ablakból futtathatod.

Parancssoros futtatáshoz nyiss meg egy x64 Visual Studio fejlesztői parancssort a projektmappában.

```powershell
cmake -S . -B out/tests -DBUILD_TESTING=ON
cmake --build out/tests --config Release --target PlumbCADUnitTests
ctest --test-dir out/tests -C Release -L unit --output-on-failure
```

A GoogleTest egységtesztjei grafikus ablak nélkül futnak. A snap, a teljes A* útvonalépítés, a projektfájl visszatöltése és a CSV-export az alkalmazásszintű tesztcsomagban is szerepel. Ehhez OpenGL 3.3 környezet szükséges.

```powershell
cmake -S . -B out/tests -DBUILD_TESTING=ON -DPLUMBCAD_BUILD_INTEGRATION_TESTS=ON
cmake --build out/tests --config Release --target PlumbCADUnitTests PlumbCADIntegrationTests
ctest --test-dir out/tests -C Release --output-on-failure
```

A tesztfuttató által létrehozott mintafájlok az `out/tests/tests/results` mappába kerülnek.



## Használat

- `1` - Mozgatás
- `2` - Forgatás
- `3` - Skála / katalógusméret váltás
- `Ctrl+Z` - Visszavonás
- `Ctrl+Y` - Újra végrehajtás

## Projekt szerkezet

```text
src/App          alkalmazáslogika
src/Geometry     mesh és transzformáció
src/Graphics     OpenGL renderelés
src/Pipeline     csőhálózat modell
src/UI           ImGui felület
src/Validation   ellenőrzések
```

## Megjegyzés

Ez a program szakdolgozati prototípus. A szabványellenőrzések demonstrációs célúak, nem helyettesítenek hivatalos mérnöki vagy szabványtanúsítási folyamatot.


## Automatikus A* csőnyomvonal

A programban két nyitott csővég között automatikus útvonal készíthető.

Használat:

1. Jelölj ki egy csőelemet.
2. Az Objektum panel `Csővégek` részén válaszd az `Útvonal kezdőpont` gombot egy nyitott véghez.
3. Egy másik nyitott végpontnál válaszd az `Útvonal végpont` gombot.
4. Nyisd meg az `Útvonal` panelt.
5. Állítsd be a rácsméretet és az akadálykerülést.
6. Nyomd meg az `Útvonal létrehozása` gombot.

A rendszer A* kereséssel 90°-os, tengelyirányú útvonalat számol, majd automatikusan létrehozza az egyenes csőszakaszokat és a 90° könyököket, és összekapcsolja őket a kiválasztott két hálózattal.


## A* útvonalpont-jelölések

- piros gyűrű: nyitott csővég
- zöld gyűrű: kapcsolt csővég
- kék gyűrű: A* útvonal kezdőpont
- lila gyűrű: A* útvonal végpont


## Több A* útvonal-lehetőség

Az Útvonal panelen az `Útvonal-lehetőségek keresése` gomb több különböző 90°-os A* nyomvonalat keres. A találatok listából választhatók ki; a lista mutatja az útvonal hosszát és a könyökök számát. Csak a `Kiválasztott útvonal létrehozása` gomb után jönnek létre a tényleges csőelemek.


## GLM / MSVC javítás

Az A* több útvonalas verzióban a `glm::vec3` visszatérési értékek explicit konstrukciót
használnak (`glm::vec3{...}`), így nem jelentkezik a
`copy-list-initialization ... cannot use an explicit constructor` fordítási hiba.


## Állítható egyenes csőhossz

Az Objektum panelen kijelölt egyenes cső hossza méterben módosítható.

- nyitott csőnél a cső a középpontja körül változtatja a hosszát
- egy kapcsolt végnél a kapcsolt vég fixen marad
- két kapcsolt végnél az egyik vég fix marad, a másik oldalhoz tartozó részhálózat együtt mozdul
- minimum egyenes csőhossz: 0.04 m
- az A* által létrehozott egyenes csövek hossza automatikusan a kiválasztott útvonal pontos szakaszhosszához igazodik


## A* sarok- és ütközésjavítás

- az A* már a teljes rácslépést vizsgálja akadály ellen, nem csak a rácspontokat
- a teljes kész útvonal újra ellenőrzésre kerül a meglévő csőelemek AABB-je ellen
- a kezdő- és végcső teste is akadály az A* keresés közben
- felesleges egymás utáni dupla 90° sarkok esetén a rendszer egyetlen sarokra egyszerűsít, ha geometriailag lehetséges
- ha két 90° könyök túl közel lenne egymáshoz, az útvonal nem kerül be a választható listába
- építéskor az egyenes csövek és könyökök csatlakozási pontjai utólag pontosan egymásra igazodnak


## A* útvonal-előnézet

Az Útvonal panelen a találati lista megmarad, miközben a lehetőségeket összehasonlítod.

- egy listaelemre kattintás csak az előnézetet váltja
- a kiválasztott útvonal sárga, vékony 3D csőként jelenik meg
- a sarokpontokat sárga jelölések mutatják
- a valódi csövek csak az `Ezt az útvonalat végleg létrehozom` gomb megnyomásakor készülnek el
- az előnézet nem része a PipeNetworknek és nem kerül mentésre


## A* végponti csatlakozás javítás

- a végpont előtti szakasz hosszabb, fix egyenes megközelítést kap
- az útvonal csak a cél csővég saját tengelyében érkezhet meg
- a találatok kevesebb könyök alapján is prioritást kapnak
- az A* által létrehozott egyenesek és 90° könyökök tengelyirányú, explicit forgatást használnak
- a végső egyenes cső hosszát szükség esetén a két tényleges csatlakozási pont között újraszámolja
- a hálózati kapcsolat csak akkor jön létre, ha a végső cső fizikailag is eléri a cél végpontot


## A* könyök/T-idom végpont javítás

- az A* kezdő- és végpontja lehet egyenes csövön, 90°/45° könyökön vagy T-idomon
- a fitting csatlakozójának kifelé mutató iránya rögzíti az A* első/utolsó lépését
- a start/end lead hossza az adott fitting tényleges világtérbeli méretéből számolódik
- a kiválasztott fitting saját AABB-je nem blokkolja a saját csatlakozási folyosóját
- a végső kapcsolat előtt a pozíció és a csatlakozási irány is ellenőrzésre kerül
- sikertelen útvonalépítéskor a program visszaállítja az építés előtti állapotot


## A* 45° útvonalak

Az automatikus útvonaltervező a 90°-os A* nyomvonalak mellett 45°-os geometriai
változatokat is keres.

- minden alkalmas 90° saroknál megvizsgálja a két 45° könyökkel kialakítható átlós rövidítést
- a 45°-os változat csak akkor kerül a listába, ha nem metszi a meglévő akadályokat
- a 45° könyök tangenciapontja `R * tan(22.5°)` szerint számolódik
- a listában külön látható a 45° és 90° könyökök száma
- a végleges építés valódi `Elbow45` és `Elbow90` elemeket hoz létre
- 45° könyök vagy más fitting kezdő/végponti iránya is használható


## Közvetlen könyök a kezdő- és végponton

Az A* már nem kényszeríti az útvonalat arra, hogy mindig egyenes csővel kezdődjön
és végződjön.

A keresési listában megjelenhetnek olyan alternatívák is, ahol:

- a kiválasztott kezdő csővégre közvetlenül 45° vagy 90° könyök csatlakozik
- azonos csőméretnél a kiválasztott végpontra közvetlenül 45° vagy 90° könyök is érkezhet
- eltérő méretnél az útvonal végére, közvetlenül az END csővég elé egy szűkítő kerül, ha egy katalógusban mindkét méret elérhető
- a szűkítő helyigénye és akadálymentessége már a keresés és az előnézet része. A START oldali csatlakozás közvetlen marad
- mindkét oldalon közvetlen könyök van
- továbbra is megmaradnak a normál, egyenes csővel induló/végződő lehetőségek

A listában a `kanyar: start`, `kanyar: vég` vagy `kanyar: start+vég` jelölés mutatja
ezeket a változatokat.


## A* csővég-ütközés védelem

Az útvonaltervező minden nem kiválasztott csővég köré külön védőzónát tesz.
Ez azért szükséges, mert a nyitott csővég a csőtest befoglaló dobozának határán van,
így korábban egy útvonal átmehetett közvetlenül a csővég közepén.

- a kiválasztott start és end csővég természetesen továbbra is átjárható
- minden más nyitott vagy kapcsolt csővég akadálynak számít
- a csővég-védőzóna figyelembe veszi a cső sugarát és az útvonal akadálytávolságát
- a keresés és a végleges építés előtti ellenőrzés is használja ezt a szabályt


## A* csőtest-kerülés újratervezve

A közvetlen kezdő/vég könyökös lehetőségek megmaradtak, de minden más meglévő
cső fizikai teste valódi akadály.

- a kiválasztott start/end elem nincs akadályként kezelve, ezért a közvetlen
  45°/90° könyökös indulás és érkezés továbbra is lehetséges
- más egyenes csőknél a valódi csőtengely több rövid akadályszakaszra van bontva
- az ütközési vastagság a meglévő és az új A* cső sugarát is figyelembe veszi
- az első, startból induló egyenes szakasz is teljes ütközésvizsgálatot kap
- a fizikai csőtesten átmenés mindig tiltott
- az extra akadálytávolság csak további biztonsági ráhagyást jelent


## Új validációk: MSZ EN 1610 + Darcy-Weisbach

### MSZ EN 1610 prototípus lejtés/lefolyás ellenőrzés

- egyenes gravitációs csőszakaszon megadható a lefolyási irány (0->1 vagy 1->0)
- a program a világ Y tengelyét függőlegesnek tekinti és kiszámítja a százalékos lejtést
- jelzi a visszaemelkedő / nem lefelé lejtő szakaszt
- a minimum és maximum lejtés projektparaméterként állítható
- ellenőrzi a két egymás után kapcsolt, bekapcsolt egyenes szakasz lefolyási irányának folytonosságát
- ez szakdolgozati prototípus-validáció, nem hivatalos szabványtanúsítás

### Darcy-Weisbach hidraulikai ellenőrzés

- a meglévő térfogatáramból és belső átmérőből számít sebességet és Reynolds-számot
- lamináris tartományban `f = 64/Re`, turbulens tartományban Swamee-Jain közelítést használ
- számít egyenes csősúrlódási nyomásveszteséget
- 45°/90° könyök, T-idom, szűkítő és szelep esetén közelítő K tényezős helyi veszteséget is hozzáad
- a beállítható elem-szintű maximális nyomásveszteséghez hasonlít
- az érdesség, sűrűség és kinematikai viszkozitás az Objektum panelen módosítható


## Routing kód tisztítás

Az egyetlen, több mint 2000 soros `ApplicationRouting.cpp` megszűnt.
A routing feladatok felelősség szerint külön fájlokba kerültek:

- `Routing/RouteGeometry.*` – tiszta geometriai, 45°/90° és ütközési segédfüggvények
- `ApplicationRoutingSelection.cpp` – start/end kiválasztás
- `ApplicationRoutingObstacles.cpp` – csőtest és csővég akadályok
- `ApplicationRoutingSearch.cpp` – A* alternatívák keresése és rendezése
- `ApplicationRoutingPreview.cpp` – sárga útvonal-előnézet
- `ApplicationRoutingBuild.cpp` – a kiválasztott útvonal valódi csőelemeinek létrehozása

A korábbi redundáns CMake explicit Application-forráslista is törölve lett, mert a
projekt már eleve rekurzívan gyűjti a `src/*.cpp` és `src/*.hpp` fájlokat.


## A* routing teljes újraírás

A korábbi routing keresőlogika (6 irányú A* + fix endpoint lead + külön 45° utófeldolgozás)
kivezetésre került.

Az új kereső:

- egységes 3D rácson dolgozik
- 18 haladási irányt használ: 6 tengelyirány + 12 síkbeli 45° irány
- az A* állapot részeként tárolja az aktuális csőirányt
- csak 0°, 45° és 90° irányváltást enged
- a 45° könyök nem utólagos módosítás: maga az A* is 45° irányban keres
- nincs fix, hosszú start/end egyenes; a létrehozott egyenes szakaszok a szükséges hosszra rövidülnek
- a start és end ponton közvetlen 45° vagy 90° könyök is használható
- több keresési lépcsőt futtat: szükség esetén finomabb ráccsal és nagyobb kerülési tartománnyal újrapróbálkozik
- minden A* rácslépés teljes szakasza ütközésvizsgálatot kap
- a végleges alternatíva ellenőrzése már a tangenciapontokra rövidített egyeneseket és a könyökök mintavételezett körívét vizsgálja
- a sárga előnézet is ezt a valódi középvonal-geometriát jeleníti meg


## A* teljesítmény és útvonal-találati javítás

A korábbi újraírás egyik hibája az volt, hogy a célpont pontos rácsra illesztéséhez
akár néhány milliméteres rácslépésig finomította a teljes 3D keresést. Ez nagy
jelenetben nagyon lassú volt, és az exact végirány miatt még így is elutasíthatta
az útvonalat.

A javított kereső:

- a megadott rácsméret környékén marad; nem finomít automatikusan 5 mm-re
- a célpontnak nem kell A* rácsponton lennie
- a cél közelében folytonos, pontos 1/2/3 szakaszos csatlakozót old meg
- a pontos csatlakozó is kizárólag 45° és 90° könyököket használhat
- a pontos csatlakozó minden szakasza akadályvizsgálatot kap
- három kontrollált fallback keresési lépcsőt használ, teljes csomópont-budgettel
- az első megtalált jó útvonalak után nem futtat feleslegesen több nagy keresést


## Validációk alapból bekapcsolva és csoportosan állíthatók

Minden újonnan létrehozott cső/idom esetén alapból aktív:

- MSZ EN 806 nyomáspróba
- MSZ EN 806 áramlási sebesség
- MSZ EN 1610 gravitációs lefolyás
- Darcy-Weisbach nyomásveszteség

Az A* Útvonal panelen egy közös validációs sablon állítható.
A sablon minden A*-ból létrehozott egyenes csőre és 45°/90° könyökre
automatikusan rákerül. Az utolsó létrehozott A* útvonal teljes elemcsoportjára
később is egyetlen gombbal újra alkalmazható.


## MSZ EN 1610 lejtés-validáció újratervezve

A korábbi csövenkénti downstream/beömlő iránykiosztás megszűnt.

Az új modellben egyetlen globális lefolyási útvonal van:

- a felhasználó kijelöl egy `kezdőpontot`
- a felhasználó kijelöl egy `végpontot`
- a víz mindig a kezdőpontból a végpont felé halad
- a program a két pont közti kapcsolt csőhálózati útvonalat automatikusan megkeresi
- az útvonal minden érintett egyenes csövén/idomán a tényleges középvonalat vizsgálja
- minden cső/idom saját minimum és maximum lejtési szöget kap fokban
- 0° vízszintes, +90° függőlegesen lefelé
- ha a középvonal a víz haladási irányában bárhol emelkedik, az `Error`
- ha nem emelkedik, de a helyi szög kimegy az elem min/max tartományából, az `Warning`

Az A* útvonal sikeres létrehozása automatikusan annak kiválasztott kezdő- és
végpontját adja át a lejtés-validációnak. Kézzel épített hálózatnál ugyanezek
a pontok az Objektum panel `Csővégek` részén külön kijelölhetők.

A projektfájl formátum v3. A v1/v2 százalékos lejtési határértékeket betöltéskor
automatikusan fokra konvertálja a program.


## EN 1610 lejtés csak egyenes csövekre

A gravitációs lejtési szög ellenőrzés kizárólag `StraightPipe` elemekre fut.

A 45°/90° könyökök és T-idomok továbbra is részei lehetnek a kijelölt
START -> END hálózati útvonalnak, de rajtuk nem történik min/max lejtési
szög ellenőrzés és nem kapnak visszaemelkedési hibát.

Új idom létrehozásakor az EN 1610 lejtés ellenőrzése kikapcsolt.
Az A* által létrehozott könyökökön is automatikusan kikapcsolva marad,
miközben az egyenes A* csöveken a közös validációs sablon szerint működik.


## Darcy-Weisbach nyomásveszteség – teljes újraírás

A korábbi elem-szintű vizsgálat helyett külön hidraulikai START -> END útvonal működik.

- a Darcy-Weisbach a közös Validáció START/END útvonalat használja
- A* útvonalépítés után ezek automatikusan az A* START/END pontok lesznek
- a program a kettő közti kapcsolt hálózati útvonalat követi
- Q, sűrűség, kinematikai viszkozitás és max. teljes veszteség globális útvonal-paraméter
- egyenes cső: Darcy-Weisbach súrlódási veszteség
- lamináris: f=64/Re
- turbulens: Swamee-Jain
- egyenes cső érdessége elemenként vagy A* sablonból állítható
- 45° könyök K=0.40; 90° könyök K=0.90
- T-idom főág K=0.60; leágazás K=1.80
- szűkítő K=0.30; szelep K=2.00
- a részveszteségek összeadódnak, majd a teljes érték a globális maximumhoz hasonlítódik

Projektformátum: v4. A v1-v3 régi HYDRAULIC_CHECK sorokból az érdesség továbbra is betöltődik.


## MSZ EN 806 áramlási sebesség – teljes újraírás

A korábbi elem-szintű áramlási ellenőrzés helyett külön START -> END útvonal működik.

- az áramlási sebesség a közös Validáció START/END útvonalat használja
- A* után ezek automatikusan az A* kezdő/végpontok lesznek
- Q, minimum és maximum sebesség globális útvonal-paraméter
- a hálózati útvonal fittingeken átmehet, de sebességet csak StraightPipe elemeken vizsgál
- minden egyenes csőn v = Q/A
- a belső átmérő a külső átmérőből és falvastagságból számítódik
- eltérő átmérőknél ugyanazon Q mellett külön sebesség adódik
- minden cső explicit MEGFELELT / NEM FELELT MEG eredményt kap
- a végén teljes START -> END összesítés készül

Projektformátum v5. A v1-v4 régi FLOW_CHECK sorok betölthetők, de az új modell nem használja az elem-szintű flow adatokat.


## MSZ EN 806 nyomáspróba – teljes újraírás

A korábbi csőelemenkénti nyomáspróba-adatok és próbanyomás-szorzó alapú
ellenőrzés megszűnt.

Az új modell egy teljes kapcsolt hálózatrészt vizsgál:

- a nyomáspróba a közös Validáció START elem teljes kapcsolt hálózatrészét használja
- nincs külön nyomáspróba-hálózat kijelölés; a START elem a referencia
- globális bemenetek: tervezési nyomás, próbanyomás, tartási idő,
  kezdeti mért nyomás, végső mért nyomás, megengedett nyomásesés
- a mért nyomásesés automatikusan: `kezdeti nyomás - végső nyomás`
- ha a végső mért nyomás nagyobb a kezdetinél, a mérési adatsor hibás
- a próbanyomásnak legalább a felhasználó által megadott tervezési nyomást el kell érnie
- nincs beégetett EN 806 próbanyomás-szorzó; ez továbbra is prototípus-szintű, konfigurálható vizsgálat
- a kapcsolt hálózat minden elemének `designPressureBar` értéke legalább a vizsgált hálózat tervezési nyomása kell legyen
- a számított nyomásesést a megengedett maximumhoz hasonlítja
- az összesítő eredmény explicit `MEGFELELT` vagy `NEM FELELT MEG`

A sikeres A* útvonalépítés automatikusan a route START elem kapcsolt
hálózatát állítja be nyomáspróba-hálózatnak.

Projektformátum: v6. A v1-v5 régi `PRESSURE_TEST` rekordok továbbra is
betölthetők; az első bekapcsolt régi elem adataiból a program migrálja az
új globális nyomáspróba alapértékeket.


## MSZ EN 806 alapadatok – teljes újraírás

Az alapadat-validáció most a projekt összes cső- és idomelemét végigvizsgálja.

Elemenként ellenőrzi:

- külső átmérő: pozitív és véges
- falvastagság: pozitív és véges
- falvastagság kisebb az átmérő felénél
- számított belső átmérő: pozitív
- anyag megnevezése: nem üres
- tervezési nyomás: pozitív és véges
- elemhossz: pozitív és véges
- csőtípus katalógusindex: érvényes
- csőméret katalógusindex: érvényes

Minden elem külön `MEGFELELT` vagy `NEM FELELT MEG` eredménysort kap.
Hibás elemnél az összes észlelt alapadat-hiba egyetlen, részletes üzenetben
jelenik meg. A vizsgálat végén külön összesítő sor mutatja a vizsgált,
megfelelt és hibás elemek számát, valamint a teljes eredményt.

Ez továbbra is szakdolgozati/prototípus-szintű adatkonzisztencia-ellenőrzés,
nem hivatalos MSZ EN 806 tanúsítás vagy beégetett szabvány-határérték adatbázis.


## Izolált elemek – teljes újraírás

Az izolált elem vizsgálat definíciója most egyértelmű:

- `0` érvényes fizikai kapcsolat: izolált, `NEM FELELT MEG`
- `1` vagy több érvényes fizikai kapcsolat: nem izolált
- a `Szándékos nyitott vég` jelölés nem számít fizikai kapcsolatnak
- a kapcsolatoknál a kölcsönös hálózati hivatkozást is ellenőrzi
- a több elemből álló, de a fő hálózattól különálló komponenseket nem ez a
  vizsgálat minősíti; azok a következő `Kapcsolt hálózat összefüggősége`
  validáció feladatai

Minden izolált elem külön `NEM FELELT MEG` eredménysort kap. A végén
összesítés készül a vizsgált, kapcsolt és izolált elemek számával, valamint
explicit `MEGFELELT / NEM FELELT MEG` eredménnyel.


## Közös validációs START / END – v8

A korábbi külön EN 806 áramlási, EN 1610 lejtési és Darcy-Weisbach
START/END kijelölések megszűntek. Az Objektum panel `Csővégek` részén
minden végponton csak két validációs gomb maradt:

- `Validáció START`
- `Validáció END`

Ugyanezt az egy közös kijelölést használja:

- az MSZ EN 806 nyomáspróba hálózatrészének meghatározása (a START elem teljes kapcsolt komponense)
- az MSZ EN 806 áramlási sebesség START -> END útvonala
- az MSZ EN 1610 gravitációs lejtés START -> END útvonala
- a Darcy-Weisbach nyomásveszteség START -> END útvonala

A teljes projektre futó hálózati ellenőrzések (nyitott végek, izolált elemek,
összefüggőség, EN 806 alapadatok) továbbra is a teljes projektet vizsgálják,
ezért azoknak nincs szükségük START/END pontra.

Az A* sikeres útvonalépítés után automatikusan beállítja a közös Validáció
START/END pontokat a route kezdő- és végpontjára.

A projektformátum v8. A v4-v7 projektek régi külön útvonalait betöltéskor
egy közös útvonallá migrálja a program. Elsőként teljes EN 806 áramlási
útvonalat, utána EN 1610 lejtési, végül Darcy útvonalat használ; ha csak
részleges régi kijelölés van, ugyanez a prioritás érvényes.


## Maximum 8 m egyenes csőhossz

Az egyenes cső maximális megengedett hossza most 8.00 m.

- kézi hosszállítás: 0.04–8.00 m
- csővég húzásával történő hosszváltoztatás: maximum 8.00 m
- új egyenes cső létrehozása: maximum 8.00 m
- A* útvonalnál a 8 m-nél hosszabb geometriai egyenes szakaszok automatikusan
  több, egymással kollineáris, legfeljebb 8 m hosszú egyenes csőre bomlanak
- a felosztás nem tesz be plusz könyököt, nem változtatja meg az útvonal
  geometriáját és ugyanaz a közös validációs START/END marad
- régi projektből betöltött 8 m-nél hosszabb egyenes csövet az EN 806
  alapadat-validáció hibaként jelzi; a betöltés nem módosítja csendben a régi
  hálózat geometriáját


## A* útvonal – pontosan forgatott kezdő/vég csatlakozás

Az automatikus útvonal már nem kényszeríti a kiválasztott kezdő- és végpont
csatlakozási tengelyét a világ 45°-os routing irányaira.

A működés:

- a program először kiolvassa a kiválasztott csővég tényleges 3D irányát
- az A* belső útvonala továbbra is a gyors 18 irányú / 45°-os routing rendszert használja
- ha a csővég tényleges iránya eltér a routing iránytól, automatikusan létrejön
  egy kis, változó szögű `AlignmentElbow`
- ez az illesztő könyök egyik végén pontosan a meglévő csővég tengelyét követi,
  másik végén pedig tangenciálisan átadja az útvonalat a diszkrét A* geometriának
- ugyanez külön megtörténik a végponton is
- a fő A* útvonal, a 45°/90° könyökök, a max. 8 m-es csődarabolás és a közös
  Validáció START/END változatlanul megmarad
- az előnézet is megjeleníti a pontos végpont-illesztő íveket
- a végleges építés újra ellenőrzi, hogy a végpont pozíciója és forgatása nem
  változott-e meg az útvonalkeresés óta
- az illesztő könyökök a projektbe mentődnek; projektformátum: v9

A Darcy-Weisbach prototípus számításban az automatikus illesztő könyök helyi
K értéke a tényleges hajlítási szögből arányosan becsült, 45°-nál K=0.40
referencia alapján. Ez továbbra sem hivatalos szabványos K-adatbázis.


## Világkoordináta-rendszer

A szerkesztő globális koordináta-rendszerének origója `(0, 0, 0)`.
Az `X` tengely piros, az `Y` tengely zöld, a `Z` tengely kék.
Az `XZ` sík a vízszintes alapsík (`Y = 0`), a pozitív `Y` irány felfelé mutat.
Forgatáskor a panel a kijelölt elem abszolút világ X/Y/Z szögeit is mutatja.
Kapcsolt 90°/45°/T-idomnál a korábbi szabály érvényes: a kiválasztott kapcsolt végpont a fix forgáspont, de az abszolút szögek továbbra is a központi világ XYZ rendszerhez viszonyíthatók.


## Nyílt validációs referenciák

A vízsebességhez nyílt ABCB/NCC, a gravitációs foul-drain lejtéshez/kapacitáshoz HM Government Approved Document H Table 6 adatokat használ. Részletek: `VALIDATION_OPEN_REFERENCES.md`.


## Ortografikus és wireframe nézet

- 3D módban a felső eszköztáron külön váltható a perspektivikus és az ortografikus vetület.
- Ortografikus módban az egérgörgő az ortografikus világméretet változtatja, nem a kamera FOV-ját.
- A Wireframe kapcsoló OpenGL `GL_LINE` poligonmódot használ, így a háromszögháló élei jelennek meg kitöltött felületek nélkül.
- A wireframe csak megjelenítési mód; a PipeNetwork, snap, A*, validáció és projektmentés geometriáját nem módosítja.


## Világ XYZ referencia megjelenítése

A világkoordináta-rendszer logikája változatlanul megmaradt: origó `(0,0,0)`, Y a függőleges tengely, XZ a vízszintes sík. A jelenet közepén korábban látható három nagy X/Y/Z tengelynyíl viszont már nincs renderelve. A kijelölt objektum kis transzformációs gizmoja továbbra is látható.


## Hálózati átmérő-ellenőrzés és csőkategóriák

- A `Hálózat összefüggőségének ellenőrzése` már a közvetlen kapcsolatok csővég-átmérőit is összeveti.
- Két eltérő átmérőjű végpont közvetlen összekapcsolása hibának számít; méretváltáshoz megfelelő szűkítő szükséges.
- A szűkítő két külön végponti átmérőjét a validáció külön kezeli.
- Kategóriaszínek: PPR = kék, PVC-U foul-drain = szürke, Réz = rézbarna.
- Az A* Útvonal panelen külön kiválasztható a létrehozandó csőkategória. A keresési logika változatlan; a kiválasztott kategóriából a START csővéghez legközelebbi katalógusméret készül.


## Validáció panel – kompakt elrendezés

A validációs panel minden szekciója alapértelmezetten csukott. A hosszú magyarázó szövegek kikerültek; a panel csak a szükséges mezőket, táblázatokat és futtatógombokat mutatja.


## UI tisztítás – javított elrendezés

- Az Objektum panelből a magyarázó szövegek és a külön `Szerkesztési eszközök` információs rész kikerült.
- A valódi, jobb alsó `Szerkesztési eszközök` ablak változatlanul megmaradt.
- Az Útvonal panelből a hosszú magyarázatok és a használati útmutató kikerültek.


## Útvonal akadálytávolság

Az `Extra akadálytávolság használata` kapcsoló kikerült az Útvonal panelből. Az `Akadálytávolság [m]` érték most mindig érvényes az A* akadálykerülésnél.


## Validációs eredmények – kompakt lista

Az **Eredmények** fül tetején az aktuális vizsgálat neve látszik. Az `ID`, `Vizsgálat` és `Részletek` oszlopban minden eredmény látszik. A vizsgálat neve hiba esetén piros, figyelmeztetésnél sárga, siker esetén zöld. A hibák száma pirossal jelenik meg a táblázat felett. A hibák és figyelmeztetések magyarázata a sor Részletek mezőjében nyitható le.

A táblázat alján a **Mentés Excelhez (CSV)** gomb az aktuális futás összes sorát és a számítási adatokat a `plumbcad_validation.csv` fájlba írja, a program munkamappájába. Az export ugyanazt a fájlt frissíti minden mentéskor.

## Table 6 hibaüzenetek

A lefolyási validáció részletei hibánál már nem hosszú nyers adatsort mutatnak. A program külön megnevezi a hibát: túl kicsi lejtés, elégtelen kapacitás, visszaemelkedés vagy nem alkalmazható csőméret, és rövid javítási javaslatot is ad.


## Kódtisztítás

Eltávolítva néhány már nem használt kódrész: a régi egérhúzásos méret-akkumulátor, két használaton kívüli validációs segédfüggvény, valamint a korábbi kézi vízsebesség min/max értékek aktív UI- és validátor-paraméterezése. A régi min/max és lejtési mezők csak a `.pcad` v8-v11 fájlkompatibilitás miatt maradtak meg a perzisztenciában.
