#!/usr/bin/env python3
"""Generate the Font Awesome icon tables from the vendored metadata.

Produces three headers under Manifold-Vault/Source/Generated/:

  FaIcons.h   -- ICON_FA_* string macros for the fixed UI chrome icons that
                 application code references by name at compile time.
  FaBrands.h  -- a sorted {name, codepoint} table of every brand icon, so a
                 platform the owner invents at runtime can name its icon as
                 text ("steam") and have it resolved by binary search. This is
                 what keeps the platform catalog data rather than a C++ enum.
  FaSymbols.h -- the fixed choice of symbols an entry can carry instead of a
                 brand mark, in display order, with search words.

Run:  python Tools/gen_icons.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
META = os.path.join(ROOT, "Vendor", "fontawesome", "metadata", "icons.json")
OUT = os.path.join(ROOT, "Manifold-Vault", "Source", "Generated")

UI_ICONS = [
    "lock", "lock-open", "unlock", "key", "eye", "eye-slash", "copy", "clock",
    "gear", "trash", "circle-plus", "pen", "magnifying-glass", "xmark", "check",
    "triangle-exclamation", "circle-info", "shield-halved", "vault", "star",
    "arrow-right-from-bracket", "rotate", "floppy-disk", "tag", "tags",
    "folder", "link", "clipboard", "dice", "circle-check", "circle-xmark",
    "bars", "chevron-right", "chevron-down", "chevron-left", "ellipsis-vertical",
    "file-export", "file-import", "qrcode", "user", "envelope", "globe", "wifi", "sort",
    "chevron-up", "arrow-rotate-left", "list",
    "layer-group", "circle-minus", "folder-plus",
    "text-height", "keyboard", "terminal", "arrow-up-right-from-square",
    "square-check", "hourglass-half",
    "palette",
]

# Names are stored in vaults (Entry::Symbol): never remove or rename one.
# The words are German search terms; English ones come from the metadata.
SYMBOLS = [
    ("building-columns", "bank banking konto finanzen sparkasse"),
    ("euro-sign", "euro geld waehrung"),
    ("dollar-sign", "dollar geld waehrung"),
    ("sterling-sign", "pfund geld waehrung"),
    ("yen-sign", "yen geld waehrung"),
    ("franc-sign", "franken geld waehrung schweiz"),
    ("bitcoin-sign", "bitcoin krypto kryptowaehrung"),
    ("coins", "muenzen geld kleingeld"),
    ("wallet", "geldboerse portemonnaie brieftasche"),
    ("credit-card", "kreditkarte karte zahlung bezahlen"),
    ("piggy-bank", "sparschwein sparen sparkonto"),
    ("money-bill-wave", "geldschein bargeld"),
    ("money-check", "scheck ueberweisung"),
    ("sack-dollar", "geldsack vermoegen"),
    ("hand-holding-dollar", "kredit darlehen spende"),
    ("chart-line", "aktien boerse depot trading kurs"),
    ("chart-pie", "statistik anteil budget haushaltsbuch"),
    ("receipt", "beleg quittung kassenbon"),
    ("file-invoice-dollar", "rechnung"),
    ("cash-register", "kasse laden verkauf"),
    ("cart-shopping", "einkaufen warenkorb shop online"),
    ("bag-shopping", "einkaufstasche mode kleidung"),
    ("basket-shopping", "einkaufskorb lebensmittel supermarkt"),
    ("store", "geschaeft laden markt"),
    ("gift", "geschenk gutschein"),
    ("tag", "angebot preis etikett"),
    ("box", "paket karton"),
    ("truck", "lieferung versand spedition"),
    ("truck-fast", "express versand paketdienst"),
    ("envelope", "mail e-mail post brief"),
    ("at", "e-mail adresse"),
    ("inbox", "posteingang"),
    ("paper-plane", "senden nachricht"),
    ("comment", "chat nachricht"),
    ("comments", "forum chat unterhaltung"),
    ("phone", "telefon anruf festnetz"),
    ("mobile-screen-button", "handy smartphone mobilfunk"),
    ("address-book", "kontakte adressbuch"),
    ("rss", "feed nachrichten"),
    ("briefcase", "arbeit job beruf"),
    ("building", "firma buero unternehmen"),
    ("id-card", "ausweis personalausweis identitaet"),
    ("id-badge", "mitarbeiterausweis dienstausweis"),
    ("address-card", "visitenkarte kontakt"),
    ("file-lines", "dokument text vertrag"),
    ("folder", "ordner ablage"),
    ("print", "drucker drucken"),
    ("calendar-days", "kalender termin"),
    ("clipboard-list", "aufgaben liste todo"),
    ("scale-balanced", "recht anwalt justiz"),
    ("gavel", "gericht auktion"),
    ("landmark", "amt behoerde staat"),
    ("flag", "land staat"),
    ("house", "zuhause haus wohnung"),
    ("house-chimney", "haus immobilie miete"),
    ("bolt", "strom energie"),
    ("fire-flame-simple", "gas heizung"),
    ("droplet", "wasser"),
    ("wifi", "wlan internet router"),
    ("plug", "anschluss stecker"),
    ("lightbulb", "licht smart home idee"),
    ("key", "schluessel zugang"),
    ("lock", "schloss sicherheit"),
    ("recycle", "recycling muell entsorgung"),
    ("heart-pulse", "gesundheit puls fitness"),
    ("hospital", "krankenhaus klinik"),
    ("stethoscope", "arzt praxis"),
    ("briefcase-medical", "krankenkasse versicherung"),
    ("kit-medical", "erste hilfe"),
    ("pills", "apotheke medikamente"),
    ("tooth", "zahnarzt zaehne"),
    ("syringe", "impfung"),
    ("plane", "flug reise flughafen"),
    ("train", "bahn zug"),
    ("bus", "bus nahverkehr"),
    ("car", "auto fahrzeug"),
    ("gas-pump", "tankstelle tanken"),
    ("bicycle", "fahrrad"),
    ("motorcycle", "motorrad"),
    ("ship", "schiff faehre kreuzfahrt"),
    ("hotel", "hotel unterkunft buchung"),
    ("suitcase", "koffer urlaub reise"),
    ("passport", "reisepass visum"),
    ("map-location-dot", "karte navigation ort"),
    ("globe", "welt internet web"),
    ("earth-europe", "europa welt"),
    ("gamepad", "spiele gaming konsole"),
    ("dice-d20", "rollenspiel wuerfel"),
    ("chess-knight", "schach strategie"),
    ("trophy", "pokal turnier wettbewerb"),
    ("puzzle-piece", "puzzle erweiterung"),
    ("film", "film kino streaming"),
    ("tv", "fernsehen streaming"),
    ("video", "video stream"),
    ("music", "musik"),
    ("headphones", "kopfhoerer hoerbuch"),
    ("podcast", "podcast"),
    ("ticket", "ticket eintritt konzert"),
    ("book", "buch bibliothek ebook"),
    ("newspaper", "zeitung nachrichten abo"),
    ("camera", "kamera foto"),
    ("image", "bild foto galerie"),
    ("palette", "kunst malen design"),
    ("futbol", "fussball sport verein"),
    ("dumbbell", "fitness studio sport"),
    ("person-running", "laufen joggen sport"),
    ("laptop", "laptop notebook"),
    ("desktop", "pc computer rechner"),
    ("server", "server hosting"),
    ("cloud", "cloud speicher online"),
    ("cloud-arrow-up", "backup sicherung upload"),
    ("database", "datenbank"),
    ("hard-drive", "festplatte speicher nas"),
    ("network-wired", "netzwerk lan"),
    ("sim-card", "sim mobilfunkvertrag"),
    ("microchip", "hardware chip"),
    ("code", "programmieren entwicklung"),
    ("terminal", "konsole shell ssh"),
    ("bug", "fehler"),
    ("robot", "roboter ki bot"),
    ("shield-halved", "sicherheit schutz antivirus vpn"),
    ("fingerprint", "fingerabdruck biometrie"),
    ("user-secret", "anonym privat geheim"),
    ("gear", "einstellungen technik"),
    ("wrench", "werkzeug reparatur"),
    ("box-archive", "archiv"),
    ("user", "person konto profil"),
    ("users", "gruppe familie team"),
    ("people-group", "verein gemeinschaft"),
    ("baby", "kind baby familie"),
    ("heart", "dating liebe partner"),
    ("thumbs-up", "soziale netzwerke"),
    ("hashtag", "social media"),
    ("graduation-cap", "uni studium hochschule"),
    ("school", "schule"),
    ("church", "kirche gemeinde"),
    ("paw", "haustier tier hund katze"),
    ("utensils", "essen restaurant lieferdienst"),
    ("pizza-slice", "pizza"),
    ("mug-hot", "cafe kaffee"),
    ("leaf", "natur umwelt bio"),
    ("seedling", "garten pflanzen"),
    ("sun", "sonne solar wetter"),
    ("star", "stern favorit"),
    ("crown", "premium vip"),
    ("gem", "schmuck wertvoll"),
    ("medal", "auszeichnung"),
    ("pen", "schreiben notiz signatur"),
]

def utf8_escape(cp):
    """C string escape of a codepoint's UTF-8 bytes, e.g. U+F1B6 -> \\xef\\x86\\xb6."""
    return "".join("\\x%02x" % b for b in chr(cp).encode("utf-8"))

def ident(name):
    return name.upper().replace("-", "_").replace(".", "_")

def to_ranges(codepoints):
    """Collapse a sorted codepoint list into inclusive [lo, hi] runs.

    ImGui wants glyph ranges as pairs. Emitting runs instead of one big
    min..max span keeps the atlas to the glyphs we actually use -- the solid
    face spans U+E000..U+F8FF, and loading all of that would be thousands of
    empty bakes.
    """
    runs = []
    for cp in codepoints:
        if runs and cp == runs[-1][1] + 1:
            runs[-1][1] = cp
        else:
            runs.append([cp, cp])
    return runs

def emit_ranges(name, codepoints):
    runs = to_ranges(sorted(set(codepoints)))
    out = ["inline constexpr ImWchar %s[] = {\n" % name]
    for lo, hi in runs:
        out.append("    0x%04X, 0x%04X,\n" % (lo, hi))
    out.append("    0,\n};\n\n")
    return out

def main():
    if not os.path.exists(META):
        sys.exit("metadata not found: %s\nIs Vendor/fontawesome populated?" % META)
    meta = json.load(open(META, encoding="utf-8"))
    os.makedirs(OUT, exist_ok=True)

    brands, solid, labels, terms, aliases = {}, {}, {}, {}, {}
    for name, info in meta.items():
        free = info.get("free") or []
        u = info.get("unicode") or ""
        if not u:
            continue
        try:
            cp = int(u, 16)
        except ValueError:
            continue
        if "brands" in free:
            brands[name] = cp
        if "solid" in free:
            solid[name] = cp
            labels[name] = info.get("label") or ""
            terms[name] = (info.get("search") or {}).get("terms") or []
            uni = (info.get("aliases") or {}).get("unicodes") or {}
            aliases[name] = [int(u, 16) for u in (uni.get("primary") or []) + (uni.get("composite") or [])
                             if all(ch in "0123456789abcdefABCDEF" for ch in u)]

    missing = [n for n in UI_ICONS if n not in solid]
    if missing:
        sys.exit("these UI icons are not in the free solid family: %s" % ", ".join(missing))

    collisions = [(n, solid[n]) for n in UI_ICONS if solid[n] < 0xE000]
    if collisions:
        sys.exit("these UI icons collide with the text font's own codepoints and must be\n"
                 "removed from UI_ICONS (use the plain character instead):\n" +
                 "\n".join("  %-20s U+%04X (%r)" % (n, cp, chr(cp)) for n, cp in collisions))

    banner = ("// Generated by Tools/gen_icons.py from Font Awesome Free 7.3.1 metadata. "
              "Do not edit.\n"
              "// Font Awesome Free: icons CC BY 4.0, fonts SIL OFL 1.1. "
              "Brand marks belong to their owners.\n"
              "#pragma once\n\n")

    ui_cps = [solid[n] for n in UI_ICONS]
    brand_cps = sorted(brands.values())

    def pua(name):
        if solid[name] >= 0xE000:
            return solid[name]
        for cp in aliases.get(name, []):
            if 0xE000 <= cp <= 0xF8FF:
                return cp
        return None

    missing = [n for n, _ in SYMBOLS if n not in solid]
    if missing:
        sys.exit("these symbols are not in the free solid family: %s" % ", ".join(missing))
    seen = set()
    for n, _ in SYMBOLS:
        if n in seen:
            sys.exit("symbol listed twice: %s" % n)
        seen.add(n)
    nopua = [n for n, _ in SYMBOLS if pua(n) is None]
    if nopua:
        sys.exit("these symbols have no private-use codepoint: %s" % ", ".join(nopua))

    def search_words(name, german):
        words = []
        for w in german.split() + labels[name].lower().split() + [t.lower() for t in terms[name]]:
            if w and w not in words and all(32 < ord(c) < 127 and c not in '"\\' for c in w):
                words.append(w)
        return " ".join(words)

    lines = [banner, '#include "imgui.h"\n\n', "namespace Bunker {\n\n"]
    lines += emit_ranges("kFaSolidRanges", ui_cps)
    lines.append("}\n\n")
    for n in UI_ICONS:
        lines.append('#define ICON_FA_%-26s "%s"\n' % (ident(n), utf8_escape(solid[n])))
    with open(os.path.join(OUT, "FaIcons.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.writelines(lines)

    ordered = sorted(brands.items())
    b = [banner, '#include "imgui.h"\n\n#include <cstddef>\n\n', "namespace Bunker {\n\n"]
    b.append("struct FaBrand { const char* name; ImWchar codepoint; };\n\n")
    b.append("inline constexpr FaBrand kFaBrands[] = {\n")
    for n, cp in ordered:
        b.append('    { "%s", 0x%04X },\n' % (n, cp))
    b.append("};\n")
    b.append("inline constexpr size_t kFaBrandCount = %d;\n\n" % len(ordered))
    b += emit_ranges("kFaBrandsRanges", brand_cps)
    b.append("}\n")
    with open(os.path.join(OUT, "FaBrands.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.writelines(b)

    y = [banner, '#include "imgui.h"\n\n#include <cstddef>\n\n', "namespace Bunker {\n\n"]
    y.append("struct FaSymbol { const char* name; ImWchar codepoint; const char* terms; };\n\n")
    y.append("inline constexpr FaSymbol kFaSymbols[] = {\n")
    for n, german in SYMBOLS:
        y.append('    { "%s", 0x%04X, "%s" },\n' % (n, pua(n), search_words(n, german)))
    y.append("};\n")
    y.append("inline constexpr size_t kFaSymbolCount = %d;\n\n" % len(SYMBOLS))
    y.append("}\n")
    with open(os.path.join(OUT, "FaSymbols.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.writelines(y)

    print("  FaSymbols.h %d symbols (solid)" % len(SYMBOLS))
    print("  FaIcons.h   %d UI icons (solid), %d range run(s)"
          % (len(UI_ICONS), len(to_ranges(sorted(set(ui_cps))))))
    print("  FaBrands.h  %d brand icons U+%04X..U+%04X, %d range run(s)"
          % (len(ordered), brand_cps[0], brand_cps[-1], len(to_ranges(brand_cps))))
    for probe in ("steam", "google", "unreal-engine", "battle-net"):
        print("    %-16s %s" % (probe, ("U+%04X" % brands[probe]) if probe in brands else "ABSENT"))
    for probe in ("epic-games", "ubisoft", "rockstar", "gog", "nintendo", "ea"):
        print("    %-16s %s" % (probe, ("U+%04X" % brands[probe]) if probe in brands
                                else "ABSENT -> monogram fallback"))

main()
