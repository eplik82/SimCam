#!/usr/bin/env python3
# =============================================================================
#  SimCam paigaldaja – esmane püsivara laadimine uude T-SIMCAM plaati (USB)
#
#  Windowsi .exe (release'i fail simcam-flasher-<versioon>.exe) sisaldab
#  püsivara simcam-factory.bin ja esptool-i – midagi muud pole vaja paigaldada.
#  Lähtekoodina: pip install esptool==4.8.1 && python simcam_flasher.py [fail.bin]
#
#  Sammud: leia plaadi USB port (Espressif VID 0x303A) → valikuline flashi
#  kustutamine → simcam-factory.bin aadressile 0x0 → taaskäivitus.
# =============================================================================
import gc
import os
import sys
import time

import esptool
from serial.tools import list_ports

VERSION = "@VERSION@"            # GitHub Actions asendab release'i versiooniga
FW_NAME = "simcam-factory.bin"
ESPRESSIF_VID = 0x303A           # ESP32-S3 sisseehitatud USB (USB-Serial/JTAG)
BAUD = 921600


def pause(msg="Vajuta Enter, et sulgeda…"):
    try:
        input(msg)
    except EOFError:
        pass


def ask(question, default=True):
    hint = "[J/e]" if default else "[j/E]"
    try:
        a = input(f"{question} {hint} ").strip().lower()
    except EOFError:
        return default
    if not a:
        return default
    return a[0] in ("j", "y")


def firmware_path():
    if len(sys.argv) > 1:
        return sys.argv[1]
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    for p in (os.path.join(base, FW_NAME), os.path.join(os.getcwd(), FW_NAME)):
        if os.path.isfile(p):
            return p
    return None


def find_port():
    """Tagastab plaadi pordi; mitme kandidaadi korral küsib kasutajalt."""
    while True:
        ports = list(list_ports.comports())
        esp = [p for p in ports if p.vid == ESPRESSIF_VID]
        if len(esp) == 1:
            p = esp[0]
            print(f"Leitud plaat: {p.device} ({p.description})")
            return p.device
        cands = esp or ports
        if cands:
            print("\nLeitud pordid:")
            for i, p in enumerate(cands, 1):
                tag = "  ← ESP32" if p.vid == ESPRESSIF_VID else ""
                print(f"  {i}) {p.device}  {p.description}{tag}")
            try:
                a = input("Vali number (Enter = otsi uuesti): ").strip()
            except EOFError:
                a = ""
            if a.isdigit() and 1 <= int(a) <= len(cands):
                return cands[int(a) - 1].device
        else:
            print("\nPlaati ei leitud. Ühenda T-SIMCAM USB-C kaabliga arvutiga.")
            print("Kui see ikka ei ilmu: hoia BOOT nuppu all, vajuta korraks RESET,")
            print("lase BOOT lahti (plaat läheb laadimisrežiimi).")
            pause("Vajuta Enter, et uuesti otsida…")
        time.sleep(0.5)


def run_esptool(args):
    print("\n> esptool " + " ".join(args))
    ok = False
    try:
        esptool.main(args)
        ok = True
    except SystemExit as e:              # esptool lõpetab vahel sys.exit()-iga
        ok = e.code in (0, None)
    except Exception as e:               # noqa: BLE001 – näita kasutajale arusaadavalt
        print(f"\nVIGA: {e}")
    # Vea korral jätab esptool pordi lahti; prügikoristus sulgeb selle, et
    # uus katse (või teine programm) saaks pordi avada.
    gc.collect()
    return ok


def main():
    print("=" * 62)
    print(f"  SimCam paigaldaja {VERSION} – LilyGO T-SIMCAM (ESP32-S3)")
    print("=" * 62)

    fw = firmware_path()
    if not fw:
        print(f"Püsivara faili {FW_NAME} ei leitud. Anna fail argumendina:")
        print("  simcam_flasher.py simcam-factory.bin")
        pause()
        return 1
    print(f"Püsivara: {os.path.basename(fw)} ({os.path.getsize(fw) // 1024} kB)")

    port = find_port()
    print("\nUue plaadi puhul tasub flash enne kustutada (LilyGO tehase tarkvara,")
    print("vanad seaded). Juba SimCam-iga plaadil kaovad siis WiFi/LTE seaded ja parool.")
    erase = ask("Kustutada kogu flash enne paigaldamist?", default=True)

    common = ["--chip", "esp32s3", "--port", port, "--baud", str(BAUD)]
    for attempt in (1, 2):
        ok = True
        if erase:
            ok = run_esptool(common + ["erase_flash"])
        if ok:
            ok = run_esptool(common + ["--after", "hard_reset", "write_flash", "-z",
                                       "--flash_mode", "keep", "--flash_freq", "keep",
                                       "--flash_size", "keep", "0x0", fw])
        if ok:
            break
        print("\nPlaadiga ei õnnestunud ühendust saada / kirjutamine katkes.")
        print("Pane plaat laadimisrežiimi: hoia BOOT all, vajuta korraks RESET, lase BOOT lahti.")
        if attempt == 1 and ask("Proovida uuesti?", default=True):
            port = find_port()
            continue
        pause()
        return 1

    print("\n" + "=" * 62)
    print("  VALMIS! SimCam püsivara on paigaldatud.")
    print("=" * 62)
    print("Kui plaat ei käivitunud ise, vajuta RESET nuppu.")
    print("1. Ühenda telefon WiFi võrku  SimCam  (parool  simcam2026)")
    print("2. Kaamera leht avaneb ise (või ava http://4.3.2.1/)")
    print("3. Logi sisse parooliga  simcam  ja muuda paroolid seadetes (⚙)")
    print("Edasised uuendused tulevad üle võrgu: ⚙ → Püsivara.")
    pause()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nKatkestatud.")
        sys.exit(1)
