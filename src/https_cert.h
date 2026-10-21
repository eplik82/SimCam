// =============================================================================
//  HttpsCert – seadmes loodud isetehtud TLS sertifikaat (ECDSA P-256)
// =============================================================================
//  Esimesel käivitusel luuakse võti + sertifikaat ja salvestatakse NVS-i
//  (privaatvõti ei ole kunagi avalikus koodis). Brauser näitab isetehtud
//  sertifikaadi puhul hoiatust – sõrmejälje järgi saab kontrollida, et
//  ühendus on just selle kaameraga.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace HttpsCert {

void usePsramForTls();        // mbedTLS mälu PSRAM-ist (TLS sessioon ~40 kB) – kutsu setup() alguses
bool ensure();                // loe NVS-ist või loo uus; true = sertifikaat olemas
const char *certPem();        // PEM (NUL-iga lõpetatud)
const char *keyPem();
const char *fingerprint();    // SHA-256 "AB:CD:…" (DER kujult), tühi kui puudub
void erase();                 // kustuta NVS-ist – uus luuakse järgmisel käivitusel

}  // namespace HttpsCert
