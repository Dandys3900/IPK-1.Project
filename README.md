[main-file-ref]: main.cpp
[udp-file-ref]: UDPClass.cpp
[tcp-file-ref]: TCPClass.cpp
[abst-file-ref]: ClientClass.cpp
[output-file-ref]: OutputClass.cpp
[mockudp-file-ref]: mockUDPserver.py
[mocktcp-file-ref]: mockTCPserver.py

# **Projektová dokumentace prvního projektu do předmětu IPK 2023/24**
**Autor:** Tomáš Daniel \
**Login:** xdanie14

## Obsah
1. [Teorie](#teorie)
2. [Struktura a implementace programu](#struct)
3. [Testování](#test)
4. [Rozšíření](#bonus)
5. [Zdroje](#source)

## Teorie <a name="teorie"></a>
V této sekci se zaměřím na základní popis teorie potřebné k ilustraci a pochopení mechanismů implementovaných v rámci tohoto projektu.
Dle mého názoru je cestou k tomuto cíli vysvětlení dvou základních transportních internetových protokolů UDP a TCP.

## **UDP (User Datagram Protocol)**
UDP je jedním z hlavních komunikačních protokolů v rámci internetového protokolového souboru.
Charakteristikou tohoto protokolu je jeho nespolehlivost doručení a současně nemožnost ověřit, zda-li data došla zamýšlenému příjemci.

Proto bylo i v rámci tohoto projektu zavedeno řešení těchto problémů. Pro ověření přijetí zprávy slouží zpráva CONFIRM, kterou je přijemce povinnen po každém úspěšném přijetí odeslat odesílateli a která obsahuje unikátní identifikátor zprávy (viz. níže). Pro vyřešení nespolehlivosti má uživatel možnost volby časového limitu pro přijetí CONFIRM zprávy, v případě, kdy dojde k překročení časového limitu, je zpráva považována na ztracenou a případně poslána znovu nebo je spojení ukončeno.

Dalším potencionálním problémem v rámci tohoto projektu při použití UDP protokolu mohou být duplikace přijatých zpráv. Kvůli povaze tohoto protokolu se totiž velmi jednoduše může stát, že odesílatel nazná, že zpráva byla ztracena při přenosu a pošle ji znovu a příjemci náhle dorazí dvě duplicitní zprávy. Toto je řešeno použitím vektoru již přijatých zpráv obsahující unikátní identifikátory zpráv, který zajisťuje, že se na duplicitní zprávu zareaguje pouze jednou a jakékoliv opakované přijetí je posléze klientem ignorováno.

Jak je ovšem možné od sebe jednotlivé zprávy rozlišit? Odpověď skryvá hlavička, která je obsažena v každé přijaté a odeslané zprávě. Tato hlavička vypadá následovně:
```
0      7 8     15 16    23 24
+--------+--------+--------+
|  Type  |    MessageID    |
+--------+--------+--------+
```
Je tvořena typem zprávy (1B) a unikátním číslem zprávy (2B). A právě unikátní číslo zprávy zajišťuje korektnost výše zmíněných řešení pro duplicitní zprávy a potvrzování přijetí zpráv.

Pro úplnost ještě doplním ukázku vlastní implementace této hlavičky v jazyce C++:
```
typedef struct {
    uint8_t type    = NO_TYPE; // 1 byte
    uint16_t msg_id = 0;       // 2 bytes
} UDP_Header;
```

Důvod pro používání tohoto protokolu v praxi je zejména nízká latence a jednoduchost použití, což převažuje výše uvedené neduhy.

## **TCP (Transmission Control Protocol)**
TCP je dalším z řady hlavním protokolů v rámci internetového protokolového souboru. Jeho hlavní výhodou je, v kontrastu s UDP, spolehlivost doručení a celkově nižší potřeba režie na straně účastníků komunikace. Narozdíl od UDP totiž TCP protokol před samotným zahájením přenosu, provede mezi oběmi zúčast stranami tzv. trojcestný handshaking, v rámci kterého dojde k nastavení a navázání spojení mezi oběmi stranami. Na druhou stranu může právě důkladná kontrola způsobit výrazné zvýšení latence přenosu, což je například pro realtimové aplikace pro živé vysílání vyloženě překážkou.

Díky tomu, že za nás tento protokol řeší drtivou většinu možných přenosových problémů, od potvrzování přijetí, zajištění správného pořadí doručení až po opětovného zasílání dat v případě ztráty, odpadá potřeba implementovat hlavičky pro zasílané zprávy.

## Struktura a implementace programu <a name="struct"></a>
Program je logicky členěn na jednotlivé soubory a funkce/metody v rámci souborů.

Na začátku každého běhu aplikace dochází ke načtení a validovaní poskytnutých CLI (Command Line Interface) argumentů a jejich následné vložení do neseřazené mapy, ze které si následně konstruktor příslušné třídy, podle uživatelem zvoleného typu spojení, tyto data načte a inicializuje své atributy.

Ukázka načtení a vložení argumentu v [hlavním souboru][main-file-ref], udávajícího komunikační port serveru, do mapy:
```
// Parse cli args
for (int index = 1; index < argc; ++index) {
    std::string cur_val(argv[index]);
    ...
    else if (cur_val == std::string("-p"))
        data_map.insert({"port", std::string(argv[++index])});
    ...
}
```

Následuje tvorba instance samotného komunikačního klienta, dle uživatele zvoleného (`UDP/TCP`) typu komunikace, který je uložen do globálního ukazatele typu `ClientClass`, zobrazeno zde:
```
TCPClass tcpClient(data_map);
UDPClass udpClient(data_map);

// Decide which client use
if (strcmp(client_type, "tcp") == 0)
    client = &tcpClient;
else
    client = &udpClient;
```

K zajištění funkčnosti výše uvedeného je třída `ClientClass` třídou abstraktní, že které jsou odvozené obě třídy pro podporované komunikační protokoly, [UDPClass][udp-file-ref] a [TCPClass][tcp-file-ref]. Tento přístup má tu výhodu, že jakékoliv další interakce v hlavním souboru s klientem jsou prováděny přes veřejné rozhraní `ClientClass` a není tedy potřeba řešit, jaký typ spojení je používán.

K běhovým chybám je přistupováno dvojím, způsobem. Pro případ nefatálních chyb nebo chyb, které nemají vliv na fungování programu je zpravidla informován uživatel výpisem obsahu chyby na standradní chybový výstup, realizováno statickou třídou [OutputClass][output-file-ref], která zároveň slouží také pro výpis zpráv přijatých ze serveru na standardní výstup. Řešení závažnějších chyb je realizováno pomocí výjimek (anglicky exceptions).

Příklad zpracování takové výjimky v [hlavním souboru][main-file-ref] při snaze klienta o navázání spojení se serverem:
```
// Try opening new connection
try {
    client->open_connection();
} catch (const std::logic_error& e) {
    OutputClass::out_err_intern(std::string(e.what()));
    return EXIT_FAILURE;
}
```

V rámci snahy o úspěšné navázání spojení se serverem dochází k vytvoření dvou pomocných [jthread](https://en.cppreference.com/w/cpp/thread/jthread) vláken, představených v rámci standardu C++20. Jmenovitě `send_thread` a `recv_thread`, deklarovaných v [ClientClass][abst-file-ref].
Tyto vlákna se starají o zpracování zpráv přijatých **ze serveru** a o odesílání zpráv **na server**.

Určitou výzvu při implementaci představovalo zajištění mezivláknové synchronizace a zabránění konfliktů při čtení a zápisu do vnitřních attributů třídy, zejména pak fronty zpráv čekající na odeslání `messages_to_send`. Pro zajištění tohoto jsou v programu použity [mutexy](https://en.cppreference.com/w/cpp/thread/mutex), [podmínečné proměnné](https://en.cppreference.com/w/cpp/thread/condition_variable) (anglicky conditional variables) a [atomické proměnné](https://en.cppreference.com/w/cpp/atomic/atomic).

Příklad použití mutexu při práci s frontou zpráv `messages_to_send` v [TCPClass][tcp-file-ref], který zabraňuje konfliktnímu čtení a zápisu do fronty:
```
{ // Mutex lock scope
    std::unique_lock<std::mutex> lock(this->editing_front_mutex);
    ...
    // Load message to send from queue front
    auto to_send = this->messages_to_send.front();
    ...
} // Mutex unlocks when getting out of scope
```

Po úspěšném spuštění klienta je jeho chování závislé na zprávách přijatých ze serveru v souladu s definovaných konečným automatem ze [zadání projektu](https://git.fit.vutbr.cz/NESFIT/IPK-Projects-2024/media/branch/master/Project%201/diagrams/protocol_fsm_client.svg).

Možností pro ukončení programu je několik:
1. Uživatel se rozhodne ukončit program zasláním interrupt signálu (`CTRL+c`)
2. Konec uživatelského vstupu (`EOF`)
3. Klient podle své vnitřní logiky rozhodne u ukončení programu (vyvolání metody *session_end();*)
4. Klient obdrží od serveru `BYE` zprávu

Určitou výzvu pro výše zmíněné představovala skutečnost, že rozhodnutí o ukončení programu je možné invokovat z různých částí programu ([main.cpp][main-file-ref], [TCPClass.cpp][tcp-file-ref] a [UDPClass.cpp][udp-file-ref]). Pro všechny případy je ovšem nezbytné korektně ukončit běžící vlákna, přičemž nemůže dojít k ukončení hrubou silou, pokud jsou vlákna v procesu, kdy je potřeba doposlat zbylé zprávy případně si na ně vyžádat odpověď, uvolnit alokované zdroje a ukončit program s příslušnou návratovou hodnotou. Tohoto bylo dosaženo použitím podmínečné proměnné deklarované v [ClientClass][abst-file-ref] v rámci hlavního programu funkce, která efektivně brání hlavní funkci v ukočení celého programu dokuď není tato proměnná "odemčena" invokováním její vnitřní funkce *notify_one()* společně s kombinací metody *wait_for_threads()* definované v [ClientClass][abst-file-ref].

Ukázka tohoto mechanismu v [main.cpp][main-file-ref]:
```
// Wait for either user EOF or thread ENDING
std::unique_lock<std::mutex> lock(end_mutex);
client->get_cond_var().wait(lock, [] {
    return (eof_event || client->stop_program());
});

if (eof_event == true) // User EOF event
    client->send_bye();
client->wait_for_threads();

// End program
return EXIT_SUCCESS;
```

## Testování <a name="test"></a>
### Testovací prostředí
Uskutečnění níže popsaných testů probíhalo v domácím prostředí v rámci lokální sítě `WLAN`, prostřednictvím internetového protokolu `IPv4`. V době testování se v síti nacházely dva následující aktivní síťové prvky:
1. Notebook hostující testovanou aplikaci `ipk24chat-client`
2. Kabelový modem [CBN CH7465](https://pics.vodafone.cz/2/kabel/compal_ch7465lg/Compal_CH7465_podrobnynavod.pdf)

V případě notebooku se jednalo o [Samsung Galaxy Book2 Pro 360](https://www.samsung.com/hk_en/computers/galaxy-book/galaxy-book2-pro-360-15inch-i7-16gb-1tb-np950qed-ka1hk/#specs), model **950QED**.

#### Systémové detaily zařízení
1. **Název operačního systému:** Microsoft Windows 11 Home
2. **Verze operačního systému:** 10.0.22631 Build 22631
3. **Výrobce operačního systému:** Microsoft Corporation
4. **Výrobce zařízení:** SAMSUNG ELECTRONICS CO., LTD.
4. **Typ systému:** x64-based PC
5. **Síťová karta:**  Intel(R) Wi-Fi 6E AX211 160MHz
    * **Connection Name:** WiFi
    * **DHCP Enabled:**    Yes

#### Testovací prostředí
Testování probíhalo v rámci hostujícího notebooku v prostředí Windows Subsystem for Linux (`WSL`), ve které byla spuštěna Linuxová distribuce **Kali Linux** (*Release:* 2023.4; *Codename:* kali-rolling). Pro simulování druhého účastníka komunikace, tedy serveru, posloužily dva Python skripty [mockTCPserver.py][mocktcp-file-ref] a [mockUDPserver.py][mockudp-file-ref].

**Oba výše uvedené testovací skripty byly v průběhu testování upravovány podle povahy a potřeb jednotlivých testů a zároveň pokud nebude v rámci jednotlivých testů uvedeno jinak, je za testovací prostředí implicitně považováno výše uvedené prostředí.**

**Symbol `->` značí příchozí zprávu na server a symbol `<-` naopak značí odchozí zprávu z serveru ke klientovi**

### Test chybějícího povinného argumenty programu
* Popis testu: Uživatel vynechá povinný argument spouštění programu *-t*, pro specifikaci typu komunikačního protokolu
* Důvody testování: Ověření schopnosti programu validovat uživatelské vstupy
* Způsob testování: Uživatel argument během spouštění vynechá
* Vstupy:
    * `./ipk24chat-client -s 127.0.0.1 -p 4567`
* Očekávaný výstup:
    * `ERROR: Compulsory values are missing` společně s návratovou hodnotou != 0
* Skutečný výstup:
```
┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
└─$ ./ipk24chat-client -s 127.0.0.1 -p 4567
ERROR: Compulsory values are missing

┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
└─$ echo $?
1
```

### Test nevalidní hodnoty pro hostname
* Popis testu: Uživatel zadá neexistující název serveru
* Důvody testování: Ověření schopnosti programu validovat uživatelské vstupy
* Způsob testování: Uživatel zadá špatnou hodnotu pro název serveru
* Vstupy:
    * `./ipk24chat-client -t udp -s NONSENSE -p 4567`
    * `./ipk24chat-client -t tcp -s NONSENSE -p 4567`
* Očekávaný výstup:
    * `ERROR: Unknown or invalid hostname provided` společně s návratovou hodnotou != 0
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s NONSENSE -p 4567
        ERROR: Unknown or invalid hostname provided

        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        1
        ```
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s NONSENSE -p 4567
        ERROR: Unknown or invalid hostname provided

        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        1
        ```

### Test ukončení programu uživatelem kombinací `CTRL+c`
* Popis testu: Ověření reakce programu na interrupt signál vyvolaný uživatelem
* Důvody testování: Požadováno dle zadání
* Způsob testování: Uživatel na začátku běhu programu provede klávesovou zkratku `CTRL+c`
* Očekávaný výstup:
    * `BYE` zpráva zaslaná serveru a korektní ukončení programu s návratovou hodnotou rovnou 0
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s 127.0.0.1 -p 4567
^C                                                              -> \xff\x00\x00 [BYE Message]
                                                                <- \x00\x00\x00 [CONFIRM Message]
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        0
        ```
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        ^C                                                      -> BYE\r\n
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        0
        ```

### Test ukončení programu uživatelem kombinací `CTRL+d`
* Popis testu: Ověření reakce programu na interrupt signál vyvolaný uživatelem
* Důvody testování: Požadováno dle zadání
* Způsob testování: Uživatel na začátku běhu programu provede klávesovou zkratku `CTRL+d`
* Očekávaný výstup:
    * `BYE` zpráva zaslaná serveru a korektní ukončení programu s návratovou hodnotou rovnou 0
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s 127.0.0.1 -p 4567
                                                                -> \xff\x00\x00 [BYE Message]
                                                                <- \x00\x00\x00 [CONFIRM Message]
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        0
        ```
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
                                                                -> BYE\r\n
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ echo $?
        0
        ```

### Test reakce na nedoručenou `REPLY` zprávu pro `AUTH` zprávu (**UDP Specific**)
* Popis testu: Ověření, že klient korektně reaguje na nedodanou, ovšem vyžadovanou, `REPLY` zprávu
* Důvody testování: Charakteristika UDP protokolu, jeden z možných edge-casů
* Způsob testování: Testovací server [mockUDPserver.py][mockudp-file-ref] při reakci na přijatou `AUTH` nezašle klientem očekávanou `REPLY` zprávu
* Očekávaný výstup:
    * Tisk chybové zprávy oznamující že server nereaguje a ukončení spojení ve formě zaslané `BYE` zprávy
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> \x02\x00\x00tom\x00tom\x00tom\x00 [AUTH Message]
                                                                <- \x00\x00\x00 [CONFIRM Message]
        ERR: Timeout for server response, ending connection     -> \xff\x00\x01 [BYE Message]
                                                                <- \x00\x00\x01 [CONFIRM Message]
        ```

### Test reakce na negativní `REPLY` zprávu pro `AUTH` zprávu
* Popis testu: Ověření a demonstrace chování klienta na negativní `REPLY` zprávu pro zaslanou `AUTH` zprávu
* Důvody testování: Jedna z běžných situací, které v rámci užívání mohou nastat
* Způsob testování: Uživatel zašle `AUTH` na kterou přijímá negativní `REPLY` odpověď
* Očekávaný výstup:
    * Tisk přijaté `REPLY` zprávy, na kterou může uživatel reagovat několika způsoby:
        * Ukončení spojení
        * Opětovnému zaslání `AUTH` zprávy
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> \x02\x00\x00tom\x00tom\x00tom\x00 [AUTH Message]
                                                                <- \x00\x00\x00 [CONFIRM Message]
                                                                <- \x01\x00\x00\x00\x00\x00nene\x00 [NEGATIVE REPLY]
        Failure: nene                                           -> \x00\x00\x00 [CONFIRM Message]
        -- MOZNOST PRO UZIVATELE ROZHODNOUT SE, CO DAL --
        ```
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> AUTH tom AS tom USING tom\r\n [AUTH Message]
                                                                <- REPLY NOK IS nene\r\n [NEGATIVE REPLY]
        Failure: nene
        -- MOZNOST PRO UZIVATELE ROZHODNOUT SE, CO DAL --
        ```

### Test reakce na neočekávanou zprávu v `OPEN` stavu
* Popis testu: Ověření reakce klienta na neočekávanou zprávu pro jeho aktuální stav
* Důvody testování: Jedna z běžných situací, které v rámci užívání mohou nastat
* Způsob testování: Klient úspěšně naváže spojení se serverem a přejde do `OPEN` stavu, ve kterém od serveru přijímá neočekávanou a tedy chybnou `AUTH` zprávu
Očekávaný výstup:
    * Tisk přijaté `REPLY` zprávy, následované výpisem chybové zprávy informující o přijetí zprávy nevalidní pro aktuální klientův stav a následné přepnutí do `ERROR` stavu, zaslání `BYE` a ukončení spojení
* Skutečný výstup:
    * UDP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t udp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> \x02\x00\x00tom\x00tom\x00tom\x00 [AUTH Message]
                                                                <- \x00\x00\x00 [CONFIRM Message]
                                                                <- \x01\x00\x00\x01\x00\x00jojo\x00 [POSITIVE REPLY]
        Success: jojo                                           -> \x00\x00\x00 [CONFIRM Message]
                                                                <- \x02\x00\x01tom\x00tom\x00tom\x00 [(Unexpected) AUTH Message]
                                                                -> \x00\x00\x01 [CONFIRM Message]
        ERR: Unexpected message received                        -> \xfe\x00\x01tom\x00Unexpected message received\x00 [ERROR Message]
                                                                <- \x00\x00\x01 [CONFIRM Message]
        ```
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> AUTH tom AS tom USING tom\r\n [AUTH Message]
                                                                <- REPLY OK IS jojo\r\n [POSITIVE REPLY]
        Success: jojo                                           <- AUTH tom AS tom USING tom\r\n [AUTH Message]
        ERR: Unexpected message received                        -> ERR FROM tom IS Unexpected message received\r\n [ERROR Message]
                                                                -> BYE\r\n
        ```

### Test reakce na přijetí vícera zpráv najednou (**TCP Specific**)
* Popis testu: Ověření, že je klient schopen rozeznat a zpracovat vícero zpráv obsažených v bufferu z funkce *recv()*
* Důvody testování: Charakteristika TCP protokolu, jeden z možných edge-casů
* Způsob testování: Testovací server [mockTCPserver.py][mocktcp-file-ref] při reakci na přijatou `AUTH` zprávu odešle klientovi zpět dvě zprávy `REPLY` a `MSG` v rámci jednoho zaslání
* Očekávaný výstup:
    * Tisk obou přijatých zpráv na standardní výstup
* Skutečný výstup:
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> AUTH tom AS tom USING tom\r\n [AUTH Message]
                                                                <- REPLY OK IS VSE JE OK\r\nMSG FROM tom IS ahoj svete\r\n [POSITIVE REPLY + MSG Message]
        Success: VSE JE OK
        tom: ahoj svete
        ```

### Test reakce na přijetí nekompletní zprávy (**TCP Specific**)
* Popis testu: Částečně navazuje na předchozí test, ověření že klient detekuje nekompletní zprávu od serveru a počká na zaslání zbytku od serveru
* Důvody testování: Charakteristika TCP protokolu, jeden z možných edge-casů
* Způsob testování: Testovací server [mockTCPserver.py][mocktcp-file-ref] při reakci na přijatou `AUTH` zprávu odešle klientovi první část zprávy `REPLY` a po 2 sekundách zbylou část
* Očekávaný výstup:
    * Tisk přijaté zprávy v jednom celku
* Skutečný výstup:
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> AUTH tom AS tom USING tom\r\n [AUTH Message]
                                                                <- REPLY OK I
                                                                ...wait 2 secs...
                                                                <- S VSE JE OK\r\n
        Success: VSE JE OK
        ```

### Test reakce na přijetí case-insensitive zprávy (**TCP Specific**)
* Popis testu: Ověření že klient na přijaté zprávy pohlíží jako na case-insensitive
* Důvody testování: Charakteristika TCP protokolu, jeden z možných edge-casů
* Způsob testování: Testovací server [mockTCPserver.py][mocktcp-file-ref] při reakci na přijatou `AUTH` zprávu odešle klientovi `REPLY` zprávu s nahodilou kombinací velkých a malých písmen (`RePlY Ok iS VsE je OK\r\n`)
* Očekávaný výstup:
    * Tisk přijaté zprávy bez jakékoliv chybové zprávy
* Skutečný výstup:
    * TCP:
        ```
        ┌──(dandys㉿DandysComp)-[~/Dandys-Kingdom/IPK-Projects/1.Project]
        └─$ ./ipk24chat-client -t tcp -s 127.0.0.1 -p 4567
        /auth tom tom tom                                       -> AUTH tom AS tom USING tom\r\n [AUTH Message]
                                                                <- RePlY Ok iS VsE je OK\r\n [POSITIVE REPLY Message]
        Success: VsE je OK
        ```

## Rozšíření <a name="bonus"></a>
V rámci tohoto projektu jsem žádná rozšíření funkcionality nad rámec zadání **neprováděl**.

## Bibliografie <a name="source"></a>

Přispěvatelé Wikipedie, User Datagram Protocol [online], Wikipedie: Otevřená encyklopedie, c2023, Datum poslední revize 18. 11. 2023, 09:48 UTC, [citováno 31. 03. 2024]. Dostupné z https://cs.wikipedia.org/w/index.php?title=User_Datagram_Protocol&oldid=23387592

Přispěvatelé Wikipedie, Transmission Control Protocol [online], Wikipedie: Otevřená encyklopedie, c2024, Datum poslední revize 31. 01. 2024, 16:40 UTC, [citováno 31. 03. 2024]. Dostupné z https://cs.wikipedia.org/w/index.php?title=Transmission_Control_Protocol&oldid=23611088>
