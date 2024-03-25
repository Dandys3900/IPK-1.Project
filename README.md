# **Projektová dokumentace prvního projektu do předmětu IPK 2023/24**
**Autor:** Tomáš Daniel \
**Login:** xdanie14

## Obsah
1. [Teorie](#teorie)
2. [Struktura programu](#struct)
3. [Testování](#test)
4. [Rozšíření](#bonus)
5. [Zdroje](#source)

## Teorie <a name="teorie"></a>
V této sekci se zaměřím na základní popis teorie potřebné k ilustraci a pochopení mechanismů implementovaných v rámci tohoto projektu.
Dle mého názoru je cestou k tomuto cíli vysvětlení dvou základních transportních internetových protokolů UDP a TCP.

## **UDP (User Datagram Protocol)**
UDP je jedním z hlavních komunikačních protokolů v rámci internetového protokolového souboru.
Charakteristikou tohoto protokolu je jeho nespolehlivost doručení a současně nemožnost ověřit, zda-li data došla zamýšlenému příjemci. Proto bylo i v rámci tohoto projektu zavedeno řešení těchto problémů. Pro ověření přijetí složí zpráva CONFIRM, kterou je přijemce povinnen po úspěšném přijetí odeslat odesílateli a pro vyřešení nespolehlivosti má uživatel možnost volby časového limitu pro přijetí CONFIRM zprávy, v případě, kdy dojde k naplnění časového limitu, je zpráva považována na ztracenou a případně poslána znovu.

Dalším potencionálním problémem v rámci tohoto projektu při použití UDP protokolu můžou být duplikace přijatých zpráv, kvůli povaze tohoto protokolu se totiž velmi jednoduše může stát, že odesílatel nazná, že zpráva byla ztracena při přenosu a pošle ji znovu a příjemci náhle dorazí dvě duplicitní zprávy. Toto je řešeno použitím vektoru již přijatých zpráv, který zajisťuje, že se na duplicitní zprávu zareaguje pouze jednou.

Jak je ovšem možné od sebe jednotlivé zprávy rozlišit? Odpověď se skrytá hlavička, která je povinná pro každou odeslanou i přijatou zprávu v rámci tohoto projektu. Tato hlavička vypadá následovně:
```
0      7 8     15 16    23 24
+--------+--------+--------+
|  Type  |    MessageID    |
+--------+--------+--------+
```
Je tvořena typem zprávy (1B) a unikátním číslem zprávy (2B). A právě unikátní číslo zprávy zajišťuje korektnost výše zmíněných řešení pro duplicitní zprávy a potvrzování přijetí zpráv.

Pro úplnost ještě doplním ukázku implementace této hlavičky v rámci mého projektu v jazyce C++:
```
typedef struct {
    uint8_t type    = NO_TYPE; // 1 byte
    uint16_t msg_id = 0;       // 2 bytes
} UDP_Header;
```

Důvod pro používání tohoto protokolu v praxi je zejména nízká latence a jednoduchost použití, což převažuje i výše uvedené neduhy.

## **TCP (Transmission Control Protocol)**
TCP je dalším hlavním protokolem v rámci internetového protokolového souboru. Jeho hlavní výhodou je spolehlivost doručení a celkově potřeba nižší režie na straně mojí implementované aplikace. Narozdíl od UDP totiž TCP protokol před samotným zahájením přenosu, provede mezi oběmi zúčast stranami tzv. handshake, který zajistí navázání spolehlivého přenosu.

Díky tomu za nás tento protokol řeší drtivou většinu možných přenosových problémů, od potvrzování přijetí až po opětovného zasílání dat v případě ztráty. A je to také důvod, proč zprávy v rámci mojí implementace této varianty nepotřebují žádnou hlavičku, narozdíl od UDP verze.

Oba protokoly jsou klíčové pro fungování internetu a mají své specifické využití v různých situacích.

## Struktura programu   <a name="struct"></a>
## Testování            <a name="test"></a>
## Rozšíření            <a name="bonus"></a>
## Zdroje               <a name="source"></a>